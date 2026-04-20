#!/usr/bin/env python3
"""
Leitura Modbus RTU do pack UNIPOWER UPLFP48 via adaptador USB-RS485.

Este script lê, por padrão, todos os registradores documentados no manual,
de 0 até 124 inclusive, e gera um CSV mantendo a mesma estrutura usada na
versão anterior:
    - endereco
    - conteudo
    - valor_hex
    - valor_unidade_esperada

Dependência:
    pip install pyserial

Exemplos de uso:
    python read_uplfp48_modbus.py --port COM3 --slave 1
    python read_uplfp48_modbus.py --port /dev/ttyUSB0 --slave 1 --baud 9600

Observações:
- O manual documenta registradores compostos em múltiplas palavras de 16 bits,
  como contadores ULONG, blocos de temperaturas e strings ASCII.
- Para preservar a mesma tabela por endereço, cada linha continua representando
  um registrador de 16 bits; quando aplicável, o campo
  "valor_unidade_esperada" inclui também o valor agregado/interpretado.
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List

import serial


# =========================
# Modbus RTU helpers
# =========================

def crc16_modbus(data: bytes) -> int:
    """Calcula CRC16 Modbus (polinômio 0xA001)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


class ModbusRTUError(Exception):
    pass


class ModbusRTUMaster:
    def __init__(
        self,
        port: str,
        baudrate: int = 19200,
        timeout: float = 1.0,
        bytesize: int = serial.EIGHTBITS,
        parity: str = serial.PARITY_NONE,
        stopbits: int = serial.STOPBITS_ONE,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.bytesize = bytesize
        self.parity = parity
        self.stopbits = stopbits
        self.ser: serial.Serial | None = None

    def __enter__(self) -> "ModbusRTUMaster":
        self.ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            bytesize=self.bytesize,
            parity=self.parity,
            stopbits=self.stopbits,
            timeout=self.timeout,
        )
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.ser and self.ser.is_open:
            self.ser.close()

    def read_holding_registers(self, slave: int, start_addr: int, quantity: int) -> List[int]:
        """Lê quantity registradores a partir de start_addr usando função 0x03."""
        if not (1 <= slave <= 0x10):
            raise ValueError("O endereço slave deve estar entre 1 e 16 (0x01 a 0x10).")
        if not (0 <= start_addr <= 0xFFFF):
            raise ValueError("start_addr fora da faixa válida.")
        if not (1 <= quantity <= 125):
            raise ValueError("quantity fora da faixa válida para Modbus RTU.")
        if self.ser is None:
            raise RuntimeError("Porta serial não aberta.")

        pdu = struct.pack(">B B H H", slave, 0x03, start_addr, quantity)
        crc = crc16_modbus(pdu)
        frame = pdu + struct.pack("<H", crc)

        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()
        self.ser.write(frame)
        self.ser.flush()

        time.sleep(0.02)

        expected_len = 5 + 2 * quantity
        response = self.ser.read(expected_len)
        if len(response) != expected_len:
            raise ModbusRTUError(
                f"Resposta incompleta: esperado {expected_len} bytes, recebido {len(response)} bytes."
            )

        resp_wo_crc = response[:-2]
        recv_crc = struct.unpack("<H", response[-2:])[0]
        calc_crc = crc16_modbus(resp_wo_crc)
        if recv_crc != calc_crc:
            raise ModbusRTUError(
                f"CRC inválido: recebido 0x{recv_crc:04X}, calculado 0x{calc_crc:04X}."
            )

        resp_slave = response[0]
        resp_func = response[1]
        byte_count = response[2]

        if resp_slave != slave:
            raise ModbusRTUError(
                f"Slave incorreto na resposta: esperado {slave}, recebido {resp_slave}."
            )

        if resp_func & 0x80:
            exc_code = response[2]
            raise ModbusRTUError(
                f"Exceção Modbus: função 0x{resp_func:02X}, código 0x{exc_code:02X}."
            )

        if resp_func != 0x03:
            raise ModbusRTUError(
                f"Função incorreta na resposta: esperado 0x03, recebido 0x{resp_func:02X}."
            )

        if byte_count != 2 * quantity:
            raise ModbusRTUError(
                f"Byte count incorreto: esperado {2 * quantity}, recebido {byte_count}."
            )

        data = response[3:-2]
        regs = list(struct.unpack(f">{quantity}H", data))
        return regs


# =========================
# Decodificação dos registradores
# =========================

def as_int16(value: int) -> int:
    return struct.unpack(">h", struct.pack(">H", value & 0xFFFF))[0]


def regs_to_ascii(regs: List[int]) -> str:
    raw = bytearray()
    for reg in regs:
        raw.extend(struct.pack(">H", reg))
    # remove nulos e espaços residuais nas extremidades
    text = raw.replace(b"\x00", b"").decode("ascii", errors="replace").strip()
    return text if text else "(vazio)"


def combine_u32(high: int, low: int) -> int:
    return ((high & 0xFFFF) << 16) | (low & 0xFFFF)


@dataclass(frozen=True)
class RegisterSpec:
    address: int
    content: str
    raw_type: str
    unit: str
    formatter: Callable[[int, List[int]], str]


def fmt_pack_voltage(raw: int, _regs: List[int]) -> str:
    return f"{raw * 0.01:.2f} V"


def fmt_signed_raw_with_note(raw: int, _regs: List[int], current_scale: float | None = None) -> str:
    signed = as_int16(raw)
    if current_scale is None:
        return (
            f"{signed} contagens (SHORT; o manual mostra unidade '10 mV' para corrente, "
            f"o que parece inconsistente)"
        )
    value_a = signed * current_scale
    return f"{value_a:.3f} A"


def fmt_cell_voltage(raw: int, _regs: List[int]) -> str:
    return f"{raw} mV"


def fmt_temperature(raw: int, _regs: List[int]) -> str:
    return f"{as_int16(raw)} °C"


def fmt_unsigned_scaled(scale: float, unit: str) -> Callable[[int, List[int]], str]:
    def _fmt(raw: int, _regs: List[int]) -> str:
        value = raw * scale
        if float(value).is_integer():
            return f"{int(value)} {unit}".strip()
        return f"{value:.3f} {unit}".strip()
    return _fmt


def fmt_unsigned_plain(unit: str) -> Callable[[int, List[int]], str]:
    def _fmt(raw: int, _regs: List[int]) -> str:
        return f"{raw} {unit}".strip()
    return _fmt


def fmt_signed_plain(unit: str) -> Callable[[int, List[int]], str]:
    def _fmt(raw: int, _regs: List[int]) -> str:
        return f"{as_int16(raw)} {unit}".strip()
    return _fmt


def decode_status(raw: int) -> str:
    mapping: Dict[int, str] = {
        0x0000: "Stand by",
        0x0001: "Recarregando",
        0x0002: "Descarregando",
        0x0004: "Protegida",
    }
    if raw in mapping:
        return mapping[raw]

    flags = []
    for bit, name in mapping.items():
        if bit != 0 and (raw & bit):
            flags.append(name)
    if flags:
        return " | ".join(flags)
    return "Desconhecido"


def decode_alarm(raw: int) -> str:
    alarm_bits = {
        0x0001: "Alarme de sobretensão",
        0x0002: "Alarme de sobretensão de célula",
        0x0004: "Alarme de subtensão",
        0x0008: "Alarme de subtensão na célula",
        0x0010: "Alarme de sobrecorrente na recarga",
        0x0020: "Alarme de sobrecorrente na descarga",
        0x0100: "Alarme de alta temperatura na recarga",
        0x0200: "Alarme de alta temperatura na descarga",
        0x0400: "Alarme de baixa temperatura na recarga",
        0x0800: "Alarme de baixa temperatura na descarga",
        0x1000: "Alarme de baixa capacidade",
        0x2000: "Falha de fusível",
        0x4000: "Alarme de módulo isolado",
    }
    if raw == 0:
        return "Sem alarme"
    return " | ".join(name for bit, name in alarm_bits.items() if raw & bit) or f"0x{raw:04X}"


def decode_protection(raw: int) -> str:
    protection_bits = {
        0x0001: "Proteção de sobretensão",
        0x0002: "Proteção de sobretensão de célula",
        0x0004: "Proteção de subtensão",
        0x0008: "Proteção de subtensão em célula",
        0x0010: "Proteção de sobrecorrente na recarga",
        0x0020: "Proteção de sobrecorrente na descarga",
        0x0100: "Proteção de alta temperatura na recarga",
        0x0200: "Proteção de alta temperatura na descarga",
        0x0400: "Proteção de baixa temperatura na recarga",
        0x0800: "Proteção de baixa temperatura na descarga",
        0x1000: "Proteção de baixa capacidade",
        0x2000: "Proteção contra curto circuito",
    }
    if raw == 0:
        return "Sem proteção ativa"
    return " | ".join(name for bit, name in protection_bits.items() if raw & bit) or f"0x{raw:04X}"


def decode_error_code(raw: int) -> str:
    error_bits = {
        0x0001: "Erro de medição de tensão",
        0x0002: "Erro de medição de temperatura",
        0x0010: "Células desbalanceadas",
    }
    if raw == 0:
        return "Sem erro"
    return " | ".join(name for bit, name in error_bits.items() if raw & bit) or f"0x{raw:04X}"


def fmt_status(raw: int, _regs: List[int]) -> str:
    return decode_status(raw)


def fmt_alarm(raw: int, _regs: List[int]) -> str:
    return decode_alarm(raw)


def fmt_protection(raw: int, _regs: List[int]) -> str:
    return decode_protection(raw)


def fmt_error_code(raw: int, _regs: List[int]) -> str:
    return decode_error_code(raw)


def fmt_u32_pair(high_addr: int, low_addr: int, unit: str = "") -> Callable[[int, List[int]], str]:
    def _fmt(raw: int, regs: List[int]) -> str:
        if high_addr < len(regs) and low_addr < len(regs):
            high = regs[high_addr]
            low = regs[low_addr]
            value = combine_u32(high, low)
            prefix = f"alto=0x{high:04X}, baixo=0x{low:04X}, combinado={value}"
            return f"{prefix} {unit}".strip()
        return f"0x{raw:04X}"
    return _fmt


def fmt_cell_temp_block(addr_index: int) -> Callable[[int, List[int]], str]:
    def _fmt(raw: int, _regs: List[int]) -> str:
        sensor_num = addr_index - 32  # 33->1, 34->2, 35->3
        return f"Temperatura de célula {sensor_num}: {raw} °C"
    return _fmt


def fmt_ascii_block(start_addr: int, end_addr: int, label: str) -> Callable[[int, List[int]], str]:
    def _fmt(_raw: int, regs: List[int]) -> str:
        if end_addr < len(regs):
            text = regs_to_ascii(regs[start_addr:end_addr + 1])
            return f"{label}: {text}"
        return "Bloco ASCII incompleto"
    return _fmt


def build_register_map(current_scale: float | None) -> Dict[int, RegisterSpec]:
    register_map: Dict[int, RegisterSpec] = {}

    # 0..1
    register_map[0] = RegisterSpec(0, "Tensão do Conjunto", "USHORT", "10 mV", fmt_pack_voltage)
    register_map[1] = RegisterSpec(
        1,
        "Corrente",
        "SHORT",
        "(manual inconsistente)",
        lambda raw, regs: fmt_signed_raw_with_note(raw, regs, current_scale),
    )

    # 2..17 células
    for addr in range(2, 18):
        cell_idx = addr - 1
        register_map[addr] = RegisterSpec(
            addr,
            f"Tensão da Célula {cell_idx:02d}",
            "USHORT",
            "mV",
            fmt_cell_voltage,
        )

    # 18..28
    register_map[18] = RegisterSpec(18, "Temperatura de resfriamento do BMS", "SHORT", "°C", fmt_temperature)
    register_map[19] = RegisterSpec(19, "Temperatura interna da bateria", "SHORT", "°C", fmt_temperature)
    register_map[20] = RegisterSpec(20, "Temperatura máxima da célula", "SHORT", "°C", fmt_temperature)
    register_map[21] = RegisterSpec(21, "Capacidade remanescente da bateria", "USHORT", "Ah", fmt_unsigned_plain("Ah"))
    register_map[22] = RegisterSpec(22, "Corrente máxima de recarga", "USHORT", "A", fmt_unsigned_plain("A"))
    register_map[23] = RegisterSpec(23, "Estado de vida (SOH)", "USHORT", "%", fmt_unsigned_plain("%"))
    register_map[24] = RegisterSpec(24, "Estado de carga (SOC)", "USHORT", "%", fmt_unsigned_plain("%"))
    register_map[25] = RegisterSpec(25, "Status", "USHORT", "", fmt_status)
    register_map[26] = RegisterSpec(26, "Alarme", "USHORT", "", fmt_alarm)
    register_map[27] = RegisterSpec(27, "Proteção", "USHORT", "", fmt_protection)
    register_map[28] = RegisterSpec(28, "Código de Erro", "USHORT", "", fmt_error_code)

    # 29..32 ULONGs
    register_map[29] = RegisterSpec(29, "Ciclo de baterias - contador (alto 16 bits)", "ULONG", "", fmt_u32_pair(29, 30))
    register_map[30] = RegisterSpec(30, "Ciclo de baterias - contador (baixo 16 bits)", "ULONG", "", fmt_u32_pair(29, 30))
    register_map[31] = RegisterSpec(31, "Capacidade a plena carga (mAs) - alto 16 bits", "ULONG", "mAs", fmt_u32_pair(31, 32, "mAs"))
    register_map[32] = RegisterSpec(32, "Capacidade a plena carga (mAs) - baixo 16 bits", "ULONG", "mAs", fmt_u32_pair(31, 32, "mAs"))

    # 33..37
    register_map[33] = RegisterSpec(33, "Temperatura de célula 1", "USHORT", "°C", fmt_cell_temp_block(33))
    register_map[34] = RegisterSpec(34, "Temperatura de célula 2", "USHORT", "°C", fmt_cell_temp_block(34))
    register_map[35] = RegisterSpec(35, "Temperatura de célula 3", "USHORT", "°C", fmt_cell_temp_block(35))
    register_map[36] = RegisterSpec(36, "Número de Células", "USHORT", "", fmt_unsigned_plain(""))
    register_map[37] = RegisterSpec(37, "Capacidade a plena carga (Ah)", "USHORT", "100 mAh", fmt_unsigned_scaled(0.1, "Ah"))

    # 38..60 reservados
    for addr in range(38, 61):
        register_map[addr] = RegisterSpec(addr, "Reservada", "USHORT", "", lambda raw, regs: f"0x{raw:04X}")

    # 61..72 tensão/proteção
    register_map[61] = RegisterSpec(61, "Dados de alarme de subtensão em célula", "USHORT", "mV", fmt_unsigned_plain("mV"))
    register_map[62] = RegisterSpec(62, "Dados de proteção de subtensão em célula", "USHORT", "mV", fmt_unsigned_plain("mV"))
    register_map[63] = RegisterSpec(63, "Dados de recuperação de proteção de subtensão em célula", "USHORT", "mV", fmt_unsigned_plain("mV"))
    register_map[64] = RegisterSpec(64, "Dados de alarme de subtensão", "USHORT", "10 mV", fmt_unsigned_scaled(0.01, "V"))
    register_map[65] = RegisterSpec(65, "Dados de proteção de subtensão", "USHORT", "10 mV", fmt_unsigned_scaled(0.01, "V"))
    register_map[66] = RegisterSpec(66, "Dados de recuperação de proteção de subtensão", "USHORT", "10 mV", fmt_unsigned_scaled(0.01, "V"))
    register_map[67] = RegisterSpec(67, "Dados de alarme de sobretensão em célula", "USHORT", "mV", fmt_unsigned_plain("mV"))
    register_map[68] = RegisterSpec(68, "Dados de proteção de sobretensão em célula", "USHORT", "mV", fmt_unsigned_plain("mV"))
    register_map[69] = RegisterSpec(69, "Dados de recuperação de proteção de sobretensão em célula", "USHORT", "mV", fmt_unsigned_plain("mV"))
    register_map[70] = RegisterSpec(70, "Dados de alarme de sobretensão", "USHORT", "10 mV", fmt_unsigned_scaled(0.01, "V"))
    register_map[71] = RegisterSpec(71, "Dados de proteção de sobretensão", "USHORT", "10 mV", fmt_unsigned_scaled(0.01, "V"))
    register_map[72] = RegisterSpec(72, "Dados de recuperação de proteção de sobretensão", "USHORT", "10 mV", fmt_unsigned_scaled(0.01, "V"))

    # 73..79 não documentados explicitamente na tabela apresentada
    for addr in range(73, 80):
        register_map[addr] = RegisterSpec(addr, "Sem descrição explícita no trecho do manual", "USHORT", "", lambda raw, regs: f"0x{raw:04X}")

    # 80..84 correntes/proteções
    register_map[80] = RegisterSpec(80, "Corrente máxima de recarga", "USHORT", "10 mA", fmt_unsigned_scaled(0.01, "A"))
    register_map[81] = RegisterSpec(81, "Corrente máxima de descarga", "USHORT", "10 mA", fmt_unsigned_scaled(0.01, "A"))
    register_map[82] = RegisterSpec(82, "Corrente de proteção de curto circuito", "USHORT", "10 mA", fmt_unsigned_scaled(0.01, "A"))
    register_map[83] = RegisterSpec(83, "Proteção de corrente de recarga", "USHORT", "10 mA", fmt_unsigned_scaled(0.01, "A"))
    register_map[84] = RegisterSpec(84, "Proteção de corrente de descarga", "USHORT", "10 mA", fmt_unsigned_scaled(0.01, "A"))

    # 85..89 não documentados explicitamente na tabela apresentada
    for addr in range(85, 90):
        register_map[addr] = RegisterSpec(addr, "Sem descrição explícita no trecho do manual", "USHORT", "", lambda raw, regs: f"0x{raw:04X}")

    # 90..101 temperaturas configuráveis
    register_map[90] = RegisterSpec(90, "Proteção Recarga (Baixa Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[91] = RegisterSpec(91, "Alarme Recarga (Baixa Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[92] = RegisterSpec(92, "Liberação de recarga (Baixa Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[93] = RegisterSpec(93, "Proteção Recarga (Alta Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[94] = RegisterSpec(94, "Alarme Recarga (Alta Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[95] = RegisterSpec(95, "Liberação Recarga (Alta Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[96] = RegisterSpec(96, "Proteção Descarga (Baixa Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[97] = RegisterSpec(97, "Alarme Descarga (Baixa Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[98] = RegisterSpec(98, "Liberação Descarga (Baixa Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[99] = RegisterSpec(99, "Proteção Descarga (Alta Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[100] = RegisterSpec(100, "Alarme Descarga (Alta Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))
    register_map[101] = RegisterSpec(101, "Liberação Descarga (Alta Temperatura)", "SHORT", "°C", fmt_signed_plain("°C"))

    # 102..104 não documentados explicitamente na tabela apresentada
    for addr in range(102, 105):
        register_map[addr] = RegisterSpec(addr, "Sem descrição explícita no trecho do manual", "USHORT", "", lambda raw, regs: f"0x{raw:04X}")

    # 105..124 blocos ASCII
    for addr in range(105, 120):
        register_map[addr] = RegisterSpec(
            addr,
            "Modelo de Bateria (Número de série)",
            "ASCII/24 bytes",
            "",
            fmt_ascii_block(105, 119, "Modelo/Número de série"),
        )

    for addr in range(117, 120):
        register_map[addr] = RegisterSpec(
            addr,
            "Versão do BMS",
            "ASCII/6 bytes",
            "",
            fmt_ascii_block(117, 119, "Versão do BMS"),
        )

    for addr in range(120, 125):
        register_map[addr] = RegisterSpec(
            addr,
            "Fabricante",
            "ASCII/10 bytes",
            "",
            fmt_ascii_block(120, 124, "Fabricante"),
        )

    return register_map


# =========================
# CSV
# =========================

def export_csv(csv_path: Path, rows: List[dict]) -> None:
    fieldnames = [
        "endereco",
        "conteudo",
        "valor_hex",
        "valor_unidade_esperada",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, delimiter=";")
        writer.writeheader()
        writer.writerows(rows)


# =========================
# CLI
# =========================

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Lê registradores Modbus RTU do pack UNIPOWER UPLFP48 e gera um CSV."
    )
    parser.add_argument("--port", required=True, help="Porta serial. Ex.: COM3 ou /dev/ttyUSB0")
    parser.add_argument("--slave", type=int, default=1, help="Endereço Modbus da bateria (1 a 16). Padrão: 1")
    parser.add_argument("--baud", type=int, default=9600, choices=[9600, 19200], help="Baud rate. Padrão: 9600")
    parser.add_argument("--timeout", type=float, default=1.0, help="Timeout serial em segundos. Padrão: 1.0")
    parser.add_argument(
        "--start-addr",
        type=int,
        default=0,
        help="Endereço inicial. Padrão: 0",
    )
    parser.add_argument(
        "--end-addr",
        type=int,
        default=124,
        help="Endereço final inclusive. Padrão: 124, cobrindo todos os registradores apresentados no manual.",
    )
    parser.add_argument(
        "--current-scale",
        type=float,
        default=None,
        help=(
            "Escala da corrente em A/LSB. Ex.: 0.01 para 10 mA/LSB. "
            "Se omitido, o script não força conversão porque a unidade da corrente está inconsistente no manual."
        ),
    )
    parser.add_argument(
        "--output",
        default="uplfp48_leitura.csv",
        help="Nome do CSV de saída. Padrão: uplfp48_leitura.csv",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.end_addr < args.start_addr:
        print("Erro: --end-addr deve ser maior ou igual a --start-addr.", file=sys.stderr)
        return 2

    register_map = build_register_map(args.current_scale)
    all_rows: List[dict] = []

    try:
        with ModbusRTUMaster(
            port=args.port,
            baudrate=args.baud,
            timeout=args.timeout,
        ) as master:
            current_addr = args.start_addr
            while current_addr <= args.end_addr:
                quantity = min(125, args.end_addr - current_addr + 1)
                regs = master.read_holding_registers(
                    slave=args.slave,
                    start_addr=current_addr,
                    quantity=quantity,
                )

                # cria uma visão local indexada pelo endereço absoluto
                local_regs = [0] * (args.end_addr + 1)
                for i, raw in enumerate(regs):
                    addr = current_addr + i
                    if addr < len(local_regs):
                        local_regs[addr] = raw

                for i, raw in enumerate(regs):
                    addr = current_addr + i
                    spec = register_map.get(
                        addr,
                        RegisterSpec(
                            address=addr,
                            content="Registrador não mapeado neste script",
                            raw_type="USHORT",
                            unit="",
                            formatter=lambda value, regs: str(value),
                        ),
                    )
                    all_rows.append(
                        {
                            "endereco": addr,
                            "conteudo": spec.content,
                            "valor_hex": f"0x{raw:04X}",
                            "valor_unidade_esperada": spec.formatter(raw, local_regs),
                        }
                    )

                current_addr += quantity

    except serial.SerialException as exc:
        print(f"Erro serial: {exc}", file=sys.stderr)
        return 1
    except ModbusRTUError as exc:
        print(f"Erro Modbus RTU: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"Erro inesperado: {exc}", file=sys.stderr)
        return 1

    csv_path = Path(args.output).resolve()
    export_csv(csv_path, all_rows)

    print("Leitura concluída com sucesso.")
    print(f"CSV gerado em: {csv_path}")
    print("Resumo:")
    for row in all_rows:
        print(
            f"  End. {row['endereco']:>3} | {row['conteudo']:<50} | "
            f"{row['valor_hex']} | {row['valor_unidade_esperada']}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

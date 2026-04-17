#!/usr/bin/env python3
"""
Leitura Modbus RTU do pack UNIPOWER UPLFP48 via adaptador USB-RS485.

O script lê, por padrão, os registradores de 0 até 25 inclusive,
pois no manual o endereço 24 é SOC e o endereço 25 é Status.

Dependência:
    pip install pyserial

Exemplo de uso:
    python scripts/read_uplfp48_modbus.py --port COM3 --slave 1
    python scripts/read_uplfp48_modbus.py --port /dev/ttyUSB0 --slave 1 --baud 9600

Saída:
    CSV com as colunas:
    - endereco
    - conteudo
    - valor_hex
    - valor_unidade_esperada
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

        # Tempo mínimo de silêncio entre quadros em 9600 bps é pequeno;
        # um pequeno atraso ajuda alguns conversores USB-RS485.
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


@dataclass(frozen=True)
class RegisterSpec:
    address: int
    content: str
    raw_type: str
    unit: str
    formatter: Callable[[int], str]


def fmt_pack_voltage(raw: int) -> str:
    return f"{raw * 0.01:.2f} V"


def fmt_signed_raw_with_note(raw: int, current_scale: float | None) -> str:
    signed = as_int16(raw)
    if current_scale is None:
        return (
            f"{signed} contagens (SHORT; o manual mostra unidade '10 mV' para corrente, "
            f"o que parece inconsistente)"
        )
    value_a = signed * current_scale
    return f"{value_a:.3f} A"


def fmt_cell_voltage(raw: int) -> str:
    return f"{raw} mV"


def fmt_temperature(raw: int) -> str:
    return f"{as_int16(raw)} °C"


def fmt_unsigned(unit: str) -> Callable[[int], str]:
    def _fmt(raw: int) -> str:
        return f"{raw} {unit}".strip()
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


def build_register_map(current_scale: float | None) -> Dict[int, RegisterSpec]:
    register_map: Dict[int, RegisterSpec] = {
        0: RegisterSpec(0, "Tensão do Conjunto", "USHORT", "V", fmt_pack_voltage),
        1: RegisterSpec(
            1,
            "Corrente",
            "SHORT",
            "A",
            lambda raw: fmt_signed_raw_with_note(raw, current_scale),
        ),
        18: RegisterSpec(18, "Temperatura de resfriamento do BMS", "SHORT", "°C", fmt_temperature),
        19: RegisterSpec(19, "Temperatura interna da bateria", "SHORT", "°C", fmt_temperature),
        20: RegisterSpec(20, "Temperatura máxima da célula", "SHORT", "°C", fmt_temperature),
        21: RegisterSpec(21, "Capacidade remanescente da bateria", "USHORT", "Ah", fmt_unsigned("Ah")),
        22: RegisterSpec(22, "Corrente máxima de recarga", "USHORT", "A", fmt_unsigned("A")),
        23: RegisterSpec(23, "Estado de vida (SOH)", "USHORT", "%", fmt_unsigned("%")),
        24: RegisterSpec(24, "Estado de carga (SOC)", "USHORT", "%", fmt_unsigned("%")),
        25: RegisterSpec(25, "Status", "USHORT", "", lambda raw: decode_status(raw)),
    }

    for addr in range(2, 18):
        cell_idx = addr - 1
        register_map[addr] = RegisterSpec(
            addr,
            f"Tensão da Célula {cell_idx:02d}",
            "USHORT",
            "mV",
            fmt_cell_voltage,
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
        default=25,
        help="Endereço final inclusive. Padrão: 25, para incluir Status.",
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

    quantity = args.end_addr - args.start_addr + 1
    register_map = build_register_map(args.current_scale)

    try:
        with ModbusRTUMaster(
            port=args.port,
            baudrate=args.baud,
            timeout=args.timeout,
        ) as master:
            regs = master.read_holding_registers(
                slave=args.slave,
                start_addr=args.start_addr,
                quantity=quantity,
            )
    except serial.SerialException as exc:
        print(f"Erro serial: {exc}", file=sys.stderr)
        return 1
    except ModbusRTUError as exc:
        print(f"Erro Modbus RTU: {exc}", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"Erro inesperado: {exc}", file=sys.stderr)
        return 1

    rows: List[dict] = []
    for i, raw in enumerate(regs):
        addr = args.start_addr + i
        spec = register_map.get(
            addr,
            RegisterSpec(
                address=addr,
                content="Registrador não mapeado neste script",
                raw_type="USHORT",
                unit="",
                formatter=lambda value: str(value),
            ),
        )
        rows.append(
            {
                "endereco": addr,
                "conteudo": spec.content,
                "valor_hex": f"0x{raw:04X}",
                "valor_unidade_esperada": spec.formatter(raw),
            }
        )

    csv_path = Path(args.output).resolve()
    export_csv(csv_path, rows)

    print("Leitura concluída com sucesso.")
    print(f"CSV gerado em: {csv_path}")
    print("Resumo:")
    for row in rows:
        print(
            f"  End. {row['endereco']:>2} | {row['conteudo']:<34} | "
            f"{row['valor_hex']} | {row['valor_unidade_esperada']}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

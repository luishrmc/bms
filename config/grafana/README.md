### Step by step setup

## Connection to FlightSQL

1. Open Grafana in your web browser.
2. Select "Connections" on the left sidebar.
3. Select "Data Sources" from the dropdown menu.
4. Click on the "Add data source" button.
5. On the bottom of the list, select "FlightSQL".
6. In the FlightSQL data source settings, enter the following details:
    - Name: `FlightSQL`
    - Host:Port - `<the_host>:8181`
    - Auth Type: `none`
    - Require TLS/SSL: `disabled`
    - key `database` with value `<the_database>`
    - key `authorization` with value `Bearer <the_authorization_token>`
7. Click on the "Save & Test" button to verify the connection.

## Importing the Dashboard

1. Open Grafana in your web browser.
2. Select "Dashboards" on the left sidebar.
3. Click on the "New" button. Then, select "Import".

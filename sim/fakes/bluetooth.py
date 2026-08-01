# fakes/bluetooth.py
"""
Fake MicroPython bluetooth module for the EMF Camp 2024 Tildagon Simulator.
Simulates GATTS, GAP advertising packet validation, and IRQ event lifecycles.
"""

# Event Constants matching MicroPython core specification
FLAG_READ = 0x0002
FLAG_WRITE = 0x0008
FLAG_NOTIFY = 0x0010
FLAG_WRITE_NO_RESPONSE = 0x0020

class UUID:
    def __init__(self, value):
        if isinstance(value, str):
            # Parse standard hex strings "6E400001-..." into raw bytes
            clean = value.replace("-", "")
            self._bytes = bytes.fromhex(clean)
        elif isinstance(value, int):
            self._bytes = value.to_bytes(2, "big")
        else:
            self._bytes = bytes(value)

    def __bytes__(self):
        return self._bytes

    def __repr__(self):
        return f"UUID({self._bytes.hex().upper()})"

    def __eq__(self, other):
        if isinstance(other, UUID):
            return self._bytes == other._bytes
        return False


class BLE:
    def __init__(self):
        self._active = False
        self._irq_callback = None
        self._gap_name = "MPY ESP32"
        self._services = []
        self._handles = {}
        self._handle_counter = 1
        self._simulated_connections = set()

        # Internal storage mirror for values written to characteristics
        self._gatt_server_db = {}

    def active(self, value=None):
        if value is not None:
            self._active = bool(value)
            if not self._active:
                self._simulated_connections.clear()
        return self._active

    def config(self, **kwargs):
        if "gap_name" in kwargs:
            self._gap_name = kwargs["gap_name"]
            return
        if kwargs:
            # Return parameter if querying single config fields
            param = list(kwargs.keys())[0]
            if param == "gap_name":
                return self._gap_name
        return None

    def irq(self, handler):
        self._irq_callback = handler

    def gatts_register_services(self, services_tuple):
        """
        Simulates standard registration.
        Returns an ordered nested tuple of integer handles mirroring the structure.
        """
        result = []
        for service_uuid, chars in services_tuple:
            char_handles = []
            for char_uuid, flags in chars:
                current_handle = self._handle_counter
                self._handles[current_handle] = {
                    "uuid": char_uuid,
                    "flags": flags,
                    "service": service_uuid
                }
                # Initialize an empty buffer for this handle slot
                self._gatt_server_db[current_handle] = b""
                char_handles.append(current_handle)
                self._handle_counter += 1
            result.append(tuple(char_handles))
        return tuple(result)

    def gap_advertise(self, interval_us, adv_data=None, scan_rsp=None):
        if not self._active:
            raise OSError("BLE hardware radio not active")

        # Hardware Safety Simulation: Catch the 31-byte limit crash early in the simulator!
        if adv_data and len(adv_data) > 31:
            print(f"\n[SIMULATOR ERROR] OSError: -30. Advertising payload is {len(adv_data)} bytes!")
            print("Max BLE legacy packet structure length is strictly 31 bytes.")
            raise OSError(-30) # Mirror the exact crash seen on the badge

        if scan_rsp and len(scan_rsp) > 31:
            print(f"\n[SIMULATOR ERROR] OSError: -30. Scan Response payload is {len(scan_rsp)} bytes!")
            raise OSError(-30)

        if adv_data:
            print(f"[SimBLE] Advertising payload registered successfully ({len(adv_data)} bytes).")

    def gatts_read(self, value_handle):
        return self._gatt_server_db.get(value_handle, b"")

    def gatts_write(self, value_handle, data):
        self._gatt_server_db[value_handle] = bytes(data)

    def gatts_notify(self, conn_handle, value_handle, data):
        if not self._active:
            return
        if conn_handle not in self._simulated_connections:
            return
        # Print output to simulator terminal block to track live telemetry
        clean_text = bytes(data).decode('utf-8', errors='replace').strip()
        print(f"[SimBLE -> Central {conn_handle}] NOTIFY Handle {value_handle}: '{clean_text}'")

    # --- SIMULATOR CONTROL FUNCTIONS (Call these from your test scripts/UI) ---

    def sim_connect_central(self, conn_handle=0):
        """Simulates a mobile app pairing with the badge."""
        if not self._active:
            return
        self._simulated_connections.add(conn_handle)
        if self._irq_callback:
            # _IRQ_CENTRAL_CONNECT = 1
            # Params match MicroPython signature: (conn_handle, addr_type, addr)
            self._irq_callback(1, (conn_handle, 0, b"\x00\x00\x00\x00\x00\x00"))

    def sim_disconnect_central(self, conn_handle=0):
        """Simulates a mobile app dropping connection or walking away."""
        if conn_handle in self._simulated_connections:
            self._simulated_connections.remove(conn_handle)
            if self._irq_callback:
                # _IRQ_CENTRAL_DISCONNECT = 2
                self._irq_callback(2, (conn_handle, 0, b"\x00\x00\x00\x00\x00\x00"))

    def sim_write_from_central(self, value_handle, data, conn_handle=0):
        """Simulates a smartphone sending steering/text data down to the RX characteristic."""
        if conn_handle not in self._simulated_connections:
            return
        # Store payload in database first
        payload_bytes = data.encode('utf-8') if isinstance(data, str) else bytes(data)
        self.gatts_write(value_handle, payload_bytes)

        if self._irq_callback:
            # _IRQ_GATTS_WRITE = 3
            self._irq_callback(3, (conn_handle, value_handle))

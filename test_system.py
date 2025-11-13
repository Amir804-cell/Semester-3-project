//test_system.py

#!/usr/bin/env python3

"""
Komplet IoT Monitoring System Integration Test.
Tests alle komponenter: Mosquitto, QuestDB, Backend, Grafana.
Run med: python3 test_system.py
"""

import sys
import time
import json
import socket
from datetime import datetime
from typing import Dict, Tuple

# Color codes for terminal output
GREEN = '\033[92m'
RED = '\033[91m'
YELLOW = '\033[93m'
BLUE = '\033[94m'
RESET = '\033[0m'

class SystemTester:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.warnings = 0
        self.results = []
        
    def print_header(self, text):
        """Print section header"""
        print(f"\n{BLUE}{'='*60}")
        print(f"  {text}")
        print(f"{'='*60}{RESET}\n")
        
    def print_test(self, name, status, message=""):
        """Print test result"""
        if status == "PASS":
            print(f"{GREEN}✓{RESET} {name}")
            if message:
                print(f"  → {message}")
            self.passed += 1
        elif status == "FAIL":
            print(f"{RED}✗{RESET} {name}")
            if message:
                print(f"  → {RED}{message}{RESET}")
            self.failed += 1
        elif status == "WARN":
            print(f"{YELLOW}⚠{RESET} {name}")
            if message:
                print(f"  → {YELLOW}{message}{RESET}")
            self.warnings += 1
        
        self.results.append({
            "test": name,
            "status": status,
            "message": message
        })
    
    def check_port(self, host, port, service_name):
        """Check if a port is open"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        result = sock.connect_ex((host, port))
        sock.close()
        
        if result == 0:
            self.print_test(f"{service_name} port {port}", "PASS", f"Port is open on {host}")
            return True
        else:
            self.print_test(f"{service_name} port {port}", "FAIL", f"Port is closed on {host}")
            return False
    
    def test_mqtt_broker(self):
        """Test MQTT Broker (Mosquitto)"""
        self.print_header("Testing MQTT Broker (Mosquitto)")
        
        try:
            import paho.mqtt.client as mqtt
            
            # Check MQTT port
            mqtt_running = self.check_port("localhost", 1883, "Mosquitto MQTT")
            
            if not mqtt_running:
                self.print_test("Mosquitto Connection", "FAIL", "MQTT broker not running")
                return False
            
            # Test MQTT connection with authentication
            connected = {"status": False}
            
            def on_connect(client, userdata, flags, rc):
                if rc == 0:
                    connected["status"] = True
                    client.disconnect()
            
            client = mqtt.Client(client_id="system_test")
            client.username_pw_set("edgeuser", "Optilogic25")
            client.on_connect = on_connect
            
            try:
                client.connect("localhost", 1883, 60)
                client.loop_start()
                time.sleep(2)
                client.loop_stop()
                
                if connected["status"]:
                    self.print_test("MQTT Authentication", "PASS", "Connected with edgeuser credentials")
                else:
                    self.print_test("MQTT Authentication", "FAIL", "Could not authenticate")
                    return False
                    
            except Exception as e:
                self.print_test("MQTT Connection", "FAIL", str(e))
                return False
            
            # Test publish/subscribe
            received = {"data": None}
            
            def on_message(client, userdata, msg):
                received["data"] = msg.payload.decode()
            
            test_client = mqtt.Client(client_id="test_pubsub")
            test_client.username_pw_set("edgeuser", "Optilogic25")
            test_client.on_message = on_message
            
            try:
                test_client.connect("localhost", 1883, 60)
                test_client.subscribe("test/system")
                test_client.loop_start()
                time.sleep(1)
                
                # Publish test message
                test_client.publish("test/system", "test_message")
                time.sleep(2)
                
                test_client.loop_stop()
                test_client.disconnect()
                
                if received["data"] == "test_message":
                    self.print_test("MQTT Pub/Sub", "PASS", "Message sent and received successfully")
                else:
                    self.print_test("MQTT Pub/Sub", "WARN", "Message not received - check topic permissions")
                    
            except Exception as e:
                self.print_test("MQTT Pub/Sub", "FAIL", str(e))
                return False
            
            return True
            
        except ImportError:
            self.print_test("MQTT Test", "FAIL", "paho-mqtt not installed. Run: pip install paho-mqtt")
            return False
        except Exception as e:
            self.print_test("MQTT Test", "FAIL", str(e))
            return False
    
    def test_questdb(self):
        """Test QuestDB Database"""
        self.print_header("Testing QuestDB Database")
        
        try:
            import psycopg2
            
            # Check QuestDB ports
            web_ui = self.check_port("localhost", 9000, "QuestDB Web UI")
            postgres = self.check_port("localhost", 8812, "QuestDB PostgreSQL")
            
            if not postgres:
                self.print_test("QuestDB Connection", "FAIL", "QuestDB not running")
                return False
            
            # Test database connection
            try:
                conn = psycopg2.connect(
                    dbname="qdb",
                    user="admin",
                    password="quest",
                    host="localhost",
                    port=8812
                )
                self.print_test("QuestDB Connection", "PASS", "Connected to database")
                
                cur = conn.cursor()
                
                # Check if sensors table exists
                cur.execute("""
                    SELECT table_name FROM information_schema.tables 
                    WHERE table_name = 'sensors'
                """)
                table_exists = cur.fetchone()
                
                if table_exists:
                    self.print_test("Sensors Table", "PASS", "Table exists")
                    
                    # Check table structure
                    cur.execute("""
                        SELECT column_name, data_type 
                        FROM information_schema.columns 
                        WHERE table_name = 'sensors'
                    """)
                    columns = cur.fetchall()
                    
                    expected_columns = ['ts', 'device_id', 'temperature', 'humidity', 'soil_moisture', 'energy']
                    found_columns = [col[0] for col in columns]
                    
                    missing_columns = set(expected_columns) - set(found_columns)
                    if missing_columns:
                        self.print_test("Table Structure", "WARN", f"Missing columns: {missing_columns}")
                    else:
                        self.print_test("Table Structure", "PASS", "All expected columns present")
                    
                    # Test insert
                    try:
                        test_ts = datetime.now()
                        cur.execute("""
                            INSERT INTO sensors (ts, device_id, temperature, humidity, soil_moisture, energy)
                            VALUES (%s, %s, %s, %s, %s, %s)
                        """, (test_ts, 'TEST_DEVICE', 25.0, 60.0, 50.0, 1.5))
                        conn.commit()
                        self.print_test("Data Insert", "PASS", "Successfully inserted test data")
                        
                        # Test query
                        cur.execute("SELECT COUNT(*) FROM sensors WHERE device_id = 'TEST_DEVICE'")
                        count = cur.fetchone()[0]
                        
                        if count > 0:
                            self.print_test("Data Query", "PASS", f"Found {count} test record(s)")
                            
                            # Clean up test data
                            cur.execute("DELETE FROM sensors WHERE device_id = 'TEST_DEVICE'")
                            conn.commit()
                            self.print_test("Data Cleanup", "PASS", "Test data removed")
                        else:
                            self.print_test("Data Query", "WARN", "Could not read inserted data")
                            
                    except Exception as e:
                        self.print_test("Data Operations", "FAIL", str(e))
                        
                else:
                    self.print_test("Sensors Table", "FAIL", "Table does not exist. Run table creation SQL.")
                    return False
                
                cur.close()
                conn.close()
                return True
                
            except psycopg2.OperationalError as e:
                self.print_test("QuestDB Connection", "FAIL", str(e))
                return False
                
        except ImportError:
            self.print_test("QuestDB Test", "FAIL", "psycopg2 not installed. Run: pip install psycopg2-binary")
            return False
        except Exception as e:
            self.print_test("QuestDB Test", "FAIL", str(e))
            return False
    
    def test_grafana(self):
        """Test Grafana"""
        self.print_header("Testing Grafana")
        
        grafana_running = self.check_port("localhost", 3000, "Grafana")
        
        if grafana_running:
            self.print_test("Grafana Status", "PASS", "Grafana is running on http://localhost:3000")
            self.print_test("Grafana Login", "WARN", "Manual verification needed: Login with admin/admin")
            return True
        else:
            self.print_test("Grafana Status", "FAIL", "Grafana not running")
            return False
    
    def test_docker_containers(self):
        """Test Docker containers"""
        self.print_header("Testing Docker Containers")
        
        try:
            import subprocess
            
            # Check if docker is running
            result = subprocess.run(['docker', 'ps'], capture_output=True, text=True)
            
            if result.returncode != 0:
                self.print_test("Docker Status", "FAIL", "Docker not running or not installed")
                return False
            
            self.print_test("Docker Status", "PASS", "Docker is running")
            
            # Check for expected containers
            containers = ['mosquitto', 'questdb', 'grafana']
            output = result.stdout
            
            for container in containers:
                if container in output:
                    self.print_test(f"{container.capitalize()} Container", "PASS", "Container is running")
                else:
                    self.print_test(f"{container.capitalize()} Container", "WARN", 
                                  f"Container not found. Start with: docker compose up -d")
            
            return True
            
        except FileNotFoundError:
            self.print_test("Docker Test", "FAIL", "Docker command not found")
            return False
        except Exception as e:
            self.print_test("Docker Test", "FAIL", str(e))
            return False
    
    def test_data_flow(self):
        """Test complete data flow: MQTT -> QuestDB"""
        self.print_header("Testing Complete Data Flow")
        
        try:
            import paho.mqtt.client as mqtt
            import psycopg2
            
            # Connect to QuestDB
            conn = psycopg2.connect(
                dbname="qdb",
                user="admin",
                password="quest",
                host="localhost",
                port=8812
            )
            cur = conn.cursor()
            
            # Get current count
            cur.execute("SELECT COUNT(*) FROM sensors WHERE device_id = 'FLOW_TEST'")
            initial_count = cur.fetchone()[0]
            
            # Publish test data via MQTT
            client = mqtt.Client(client_id="flow_test")
            client.username_pw_set("edgeuser", "Optilogic25")
            client.connect("localhost", 1883, 60)
            
            test_payload = "FLOW_TEST:123"
            client.publish("sensors/test", test_payload)
            client.disconnect()
            
            self.print_test("Data Published", "PASS", f"Sent: {test_payload}")
            
            # Wait for backend to process
            time.sleep(3)
            
            # Check if data arrived in QuestDB
            cur.execute("SELECT COUNT(*) FROM sensors WHERE device_id = 'FLOW_TEST'")
            final_count = cur.fetchone()[0]
            
            if final_count > initial_count:
                self.print_test("Data Flow", "PASS", 
                              f"Data successfully flowed: MQTT → Backend → QuestDB")
                
                # Clean up
                cur.execute("DELETE FROM sensors WHERE device_id = 'FLOW_TEST'")
                conn.commit()
            else:
                self.print_test("Data Flow", "WARN", 
                              "Data not found in database. Check if backend/simulate_sensors.py is running")
            
            cur.close()
            conn.close()
            return True
            
        except Exception as e:
            self.print_test("Data Flow Test", "FAIL", str(e))
            return False
    
    def test_backend_script(self):
        """Test if backend simulation script is configured"""
        self.print_header("Testing Backend Configuration")
        
        import os
        
        backend_path = os.path.expanduser("~/iot-monitoring/backend")
        simulate_script = os.path.join(backend_path, "simulate_sensors.py")
        
        if os.path.exists(simulate_script):
            self.print_test("Backend Script", "PASS", f"Found: {simulate_script}")
            
            # Check if script is running
            try:
                import subprocess
                result = subprocess.run(['pgrep', '-f', 'simulate_sensors.py'], 
                                      capture_output=True, text=True)
                if result.stdout.strip():
                    self.print_test("Backend Process", "PASS", "simulate_sensors.py is running")
                else:
                    self.print_test("Backend Process", "WARN", 
                                  "simulate_sensors.py not running. Start with: python3 simulate_sensors.py")
            except:
                self.print_test("Backend Process", "WARN", "Could not check process status")
        else:
            self.print_test("Backend Script", "WARN", f"Not found: {simulate_script}")
        
        return True
    
    def print_summary(self):
        """Print test summary"""
        print(f"\n{BLUE}{'='*60}")
        print("  TEST SUMMARY")
        print(f"{'='*60}{RESET}\n")
        
        total = self.passed + self.failed + self.warnings
        
        print(f"{GREEN}Passed:{RESET}   {self.passed}/{total}")
        print(f"{RED}Failed:{RESET}   {self.failed}/{total}")
        print(f"{YELLOW}Warnings:{RESET} {self.warnings}/{total}")
        
        if self.failed == 0:
            print(f"\n{GREEN}✓ System is operational!{RESET}")
            return True
        else:
            print(f"\n{RED}✗ System has issues that need attention.{RESET}")
            return False
    
    def run_all_tests(self):
        """Run all system tests"""
        print(f"{BLUE}")
        print("╔════════════════════════════════════════════════════════╗")
        print("║     IoT Monitoring System - Integration Test          ║")
        print("║     Testing all components in sequence                ║")
        print("╚════════════════════════════════════════════════════════╝")
        print(f"{RESET}")
        
        # Run tests in order
        self.test_docker_containers()
        self.test_mqtt_broker()
        self.test_questdb()
        self.test_grafana()
        self.test_backend_script()
        self.test_data_flow()
        
        # Print summary
        success = self.print_summary()
        
        # Recommendations
        if self.failed > 0 or self.warnings > 0:
            print(f"\n{YELLOW}RECOMMENDATIONS:{RESET}")
            print("─" * 60)
            
            if self.failed > 0:
                print("\n1. Check Docker containers are running:")
                print("   docker compose ps")
                print("\n2. Restart failed services:")
                print("   docker compose restart")
                print("\n3. Check service logs:")
                print("   docker logs mosquitto")
                print("   docker logs questdb")
                print("   docker logs grafana")
            
            if self.warnings > 0:
                print("\n4. Start backend simulation:")
                print("   cd ~/iot-monitoring/backend")
                print("   source venv/bin/activate")
                print("   python3 simulate_sensors.py")
                print("\n5. Verify Grafana datasource:")
                print("   Open http://localhost:3000")
                print("   Add QuestDB as PostgreSQL datasource")
        
        print("\n" + "─" * 60 + "\n")
        
        return success


def main():
    """Main function"""
    tester = SystemTester()
    
    try:
        success = tester.run_all_tests()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print(f"\n{YELLOW}Test interrupted by user{RESET}")
        sys.exit(1)
    except Exception as e:
        print(f"\n{RED}Unexpected error: {e}{RESET}")
        sys.exit(1)


if __name__ == "__main__":
    main()

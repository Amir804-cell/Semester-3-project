#!/bin/bash

###############################################################################
# Quick System Check Script
# Rapidly checks if all services are running
###############################################################################

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗"
echo -e "║     IoT Monitoring System - Quick Check                ║"
echo -e "╚════════════════════════════════════════════════════════╝${NC}\n"

# Check Docker
echo -e "${BLUE}[1/6] Checking Docker...${NC}"
if command -v docker &> /dev/null; then
    if docker ps &> /dev/null; then
        echo -e "${GREEN}✓ Docker is running${NC}"
    else
        echo -e "${RED}✗ Docker is installed but not running${NC}"
        echo -e "  → Start with: sudo systemctl start docker"
    fi
else
    echo -e "${RED}✗ Docker is not installed${NC}"
fi

# Check Docker Containers
echo -e "\n${BLUE}[2/6] Checking Containers...${NC}"
if docker ps --format "table {{.Names}}\t{{.Status}}" | grep -E "mosquitto|questdb|grafana" &> /dev/null; then
    docker ps --format "table {{.Names}}\t{{.Status}}" | grep -E "mosquitto|questdb|grafana" | while read line; do
        if echo "$line" | grep -q "Up"; then
            echo -e "${GREEN}✓${NC} $line"
        else
            echo -e "${RED}✗${NC} $line"
        fi
    done
else
    echo -e "${YELLOW}⚠ No containers running${NC}"
    echo -e "  → Start with: cd ~/iot-monitoring && docker compose up -d"
fi

# Check Mosquitto
echo -e "\n${BLUE}[3/6] Checking Mosquitto (MQTT)...${NC}"
if nc -zv localhost 1883 &> /dev/null; then
    echo -e "${GREEN}✓ Mosquitto is accessible on port 1883${NC}"
else
    echo -e "${RED}✗ Mosquitto is not accessible${NC}"
fi

# Check QuestDB
echo -e "\n${BLUE}[4/6] Checking QuestDB...${NC}"
if nc -zv localhost 9000 &> /dev/null; then
    echo -e "${GREEN}✓ QuestDB Web UI on port 9000${NC}"
else
    echo -e "${RED}✗ QuestDB Web UI not accessible${NC}"
fi

if nc -zv localhost 8812 &> /dev/null; then
    echo -e "${GREEN}✓ QuestDB PostgreSQL on port 8812${NC}"
else
    echo -e "${RED}✗ QuestDB PostgreSQL not accessible${NC}"
fi

# Check Grafana
echo -e "\n${BLUE}[5/6] Checking Grafana...${NC}"
if nc -zv localhost 3000 &> /dev/null; then
    echo -e "${GREEN}✓ Grafana is accessible on port 3000${NC}"
else
    echo -e "${RED}✗ Grafana is not accessible${NC}"
fi

# Check Backend Script
echo -e "\n${BLUE}[6/6] Checking Backend Script...${NC}"
if pgrep -f "simulate_sensors.py" > /dev/null; then
    echo -e "${GREEN}✓ simulate_sensors.py is running${NC}"
else
    echo -e "${YELLOW}⚠ simulate_sensors.py is not running${NC}"
    echo -e "  → Start with: cd ~/iot-monitoring/backend && python3 simulate_sensors.py &"
fi

# Summary
echo -e "\n${BLUE}════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}SUMMARY${NC}"
echo -e "${BLUE}════════════════════════════════════════════════════════${NC}\n"

echo -e "Service URLs:"
echo -e "  • QuestDB Console:  ${BLUE}http://localhost:9000${NC}"
echo -e "  • Grafana:          ${BLUE}http://localhost:3000${NC} (admin/admin)"
echo -e "  • Mosquitto MQTT:   ${BLUE}localhost:1883${NC}"

echo -e "\nNext Steps:"
echo -e "  1. Run full test:   ${BLUE}python3 test_system.py${NC}"
echo -e "  2. Check logs:      ${BLUE}docker logs <container_name>${NC}"
echo -e "  3. View data:       ${BLUE}http://localhost:9000${NC}"

echo ""

# Gør det executable med command: chmod +x quick_check.sh

# Quick check (30 seconds): ./quick_check.sh 

# Full test (1-2 minutes) ::python3 test_system.py

# Svarende til health checks


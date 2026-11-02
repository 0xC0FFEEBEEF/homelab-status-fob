// Everything the fob knows, shared by all designs.
#pragma once
#include <Arduino.h>

struct Node { const char *name; uint8_t load; int8_t temp; bool up; };
struct Svc  { const char *name; bool up; uint16_t ms; };

struct Status {
  // slerm LLM cluster
  float       tokS;
  uint8_t     queue;
  const char *model;
  Node        nodes[5];
  uint8_t     nodeCount;

  // proxmox hosts
  uint8_t  bmCpu, bmRam, bm2Cpu, bm2Ram;
  uint8_t  vms, cts;

  // power / UPS
  uint8_t  battPct;
  bool     onBattery;
  uint16_t loadW;
  uint16_t runtimeMin;

  // wazuh
  uint16_t alerts;
  uint8_t  sev[4];              // crit, high, med, low
  uint8_t  agentsUp, agentsTotal;
  char     lastAlert[48];

  // services
  Svc      svcs[6];
  uint8_t  svcCount;

  // net + clock
  int8_t   rssi;
  bool     wanUp;
  uint16_t wanMs;
  uint32_t uptimeMin;
  uint8_t  hh, mm;
};

extern Status g;

void dataInit();
void dataUpdate(uint32_t nowMs, float dt);

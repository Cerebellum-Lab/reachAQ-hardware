# General
- [x] Design rules set appropriately for fab house?
  - Which Fab is being used? : JLCPCB
  - How many PCB layers?:
    - [ ] 2
    - [x] 4
    - [ ] 6
    - [ ] 8
  - Special PCB Requirements?:
    - [ ] Blind Buried Vias
    - [ ] Capped Vias
    - [ ] Rigid/Flex
- [x] ERC passes?
  - [x] ERC report included?
  - Warnings are due to CANID pins being pulled to ground and are to be expected.
- [x] DRC passes?
  - [x] DRC report included?
- [x] Appropriate copper weight selected?

# BOM
- [x] Components in stock?
- [x] Specialty components recommended for new designs?
- [x] All specialty components added to design library?
- [x] Resistors / Capacitors rated appropriately?

# Schematic
## General
- [x] Net classes assigned for critical nets?
- [x] Test points added to critical nets?
- [x] Gound points near test points?
- [x] Proper and judicious use of heirarchical schematics? 
- [x] Diodes properly oriented?
- [x] Schematic included in manufacturing folder?

## Digital
- [x] Decoupling caps on all digital chips?
- [x] Inputs appropriately protected?
- [x] Bus signals appropriately terminated?
	- [x] I2C Terminated?
	- [x] SPI tied off?
- [x] Ferrite beads placed on high speed / high noise ICs?
	- PLLs
	- Oscillators
	- Chips with very high speeds

## MCU
- [x] JTAG broken out?
- [x] LED/Debug IO broken out?
- [x] Able to initiate bootloader?
  - i.e. are boot/reset lines broken out to jumper/button?
- [x] GPIOs don't interfere with boot/reset pins

## Power Electronics
- [x] Proper capacitance on both sides of load switches?
- [x] Power supplies rated appropriately?
- [x] Power supplies have sufficient bulk capacitance?
- [x] All components connected to correct power net?
- [x] Voltage drop accounted for over cables?

# Layout
- [x] Silkscreen Complete?
	- [x] Version?
	- [x] Name?
	- [x] Connectors labeled?
    - [x] Designators placed/spaced and sized properly for all components?
	- [x] Silkscreen doesn't overlap with drill hits or other features that'd make it illegible?
- [x] Design properly floorplanned?
	- [x] Analog / Digital / Power separated?
- [x] Differential signals skew matched?
- [x] Clocked data lines skew matched?
- [x] Routing only on appropriate layers?
- [x] Appropriate separation for high voltage?
- [x] Elements with >1 row of pin headers have their own footprint?
- [x] (If ordering via gerber upload) Gerber files included in manufacturing folder?


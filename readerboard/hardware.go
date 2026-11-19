//
// This needs to be refactored. It made more sense when all devices were variations on the
// same hardware but now there should be more object orientation in the drivers with
// abstraction being used to organize this better so there aren't all these little
// device-specific special cases sprinkled around all over.
//

package readerboard

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"regexp"
	"strings"
	"time"

	"github.com/MadScienceZone/go-gma/v5/util"
	"go.bug.st/serial"
)

type LightList []byte

type LEDSequence struct {
	IsRunning    bool
	CustomTiming struct {
		Enabled bool
		Up      float64
		On      float64
		Down    float64
		Off     float64
	}
	Position int
	Sequence LightList
}

func (v LightList) MarshalJSON() ([]byte, error) {
	return json.Marshal(string(v))
}

func (v *LightList) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return err
	}
	*v = []byte(s)
	return nil
}

type EEPROMType byte

const (
	NoEEPROM EEPROMType = iota
	ExternalEEPROM
	InternalEEPROM
)

type SoundType byte

const (
	NoSound SoundType = iota
	SingleTone
	FullTonal
)

func (n EEPROMType) MarshalJSON() ([]byte, error) {
	switch n {
	case NoEEPROM:
		return json.Marshal("none")
	case ExternalEEPROM:
		return json.Marshal("external")
	case InternalEEPROM:
		return json.Marshal("internal")
	}
	return nil, fmt.Errorf("Unsupported EEPROMType value %v", n)
}

func (n *EEPROMType) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return err
	}
	switch s {
	case "none":
		*n = NoEEPROM
	case "external", "ext":
		*n = ExternalEEPROM
	case "internal", "int":
		*n = InternalEEPROM
	default:
		return fmt.Errorf("Unsupported EEPROMType value %v", s)
	}
	return nil
}

func (n SoundType) MarshalJSON() ([]byte, error) {
	switch n {
	case NoSound:
		return json.Marshal("none")
	case SingleTone:
		return json.Marshal("single")
	case FullTonal:
		return json.Marshal("full")
	}
	return nil, fmt.Errorf("Unsupported SoundType value %v", n)
}

func (n *SoundType) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return err
	}
	switch s {
	case "none":
		*n = NoSound
	case "single":
		*n = SingleTone
	case "full":
		*n = FullTonal
	default:
		return fmt.Errorf("Unsupported SoundType value %v", s)
	}
	return nil
}

func parseSoundType(e byte) (SoundType, error) {
	switch e {
	case 'S':
		return SingleTone, nil
	case 'T':
		return FullTonal, nil
	case '_':
		return NoSound, nil
	}
	return NoSound, fmt.Errorf("invalid sount type code %v", e)
}

func parseEEPROMType(e byte) (EEPROMType, error) {
	switch e {
	case 'I':
		return InternalEEPROM, nil
	case 'X':
		return ExternalEEPROM, nil
	case '_':
		return NoEEPROM, nil
	}
	return NoEEPROM, fmt.Errorf("invalid EEPROM code %v", e)
}

func EEPROMTypeName(e EEPROMType) string {
	switch e {
	case InternalEEPROM:
		return "internal"
	case ExternalEEPROM:
		return "external"
	case NoEEPROM:
		return "no"
	default:
		return "UNKNOWN"
	}
}

func SoundTypeName(e SoundType) string {
	switch e {
	case SingleTone:
		return "single"
	case FullTonal:
		return "full"
	case NoSound:
		return "no"
	default:
		return "UNKNOWN"
	}
}

type ModelClass byte

func (m ModelClass) MarshalJSON() ([]byte, error) {
	return json.Marshal(ModelClassName(m))
}

func (m *ModelClass) UnmarshalJSON(j []byte) error {
	var s string
	if err := json.Unmarshal(j, &s); err != nil {
		return err
	}
	switch s {
	case "Busylight":
		*m = 'B'
	case "Readerboard_RGB":
		*m = 'C'
	case "Readerboard_Monochrome":
		*m = 'M'
	default:
		return fmt.Errorf("unknown model class value")
	}
	return nil
}

func ModelClassName(m ModelClass) string {
	switch m {
	case 'B':
		return "Busylight"
	case 'C':
		return "Readerboard_RGB"
	case 'M':
		return "Readerboard_Monochrome"
	default:
		return "Unknown"
	}
}

type DeviceStatus struct {
	DeviceModelClass ModelClass
	DeviceAddress    byte
	GlobalAddress    byte
	SpeedUSB         int
	Speed485         int
	EEPROM           EEPROMType
	Sound            SoundType
	HardwareRevision string
	FirmwareRevision string
	Serial           string
	StatusLEDs       DiscreteLEDStatus
	MatrixWidth      int
	MatrixHeight     int
	ImageBitmap      [][64]byte
	Dimmers          []DimmerSet
	DimmerValid      []bool
}

type DimmerSet byte

func (ds DimmerSet) MarshalJSON() ([]byte, error) {
	return json.Marshal(int(ds))
}
func (ds *DimmerSet) UnmarshalJSON(j []byte) error {
	var d int
	if err := json.Unmarshal(j, &d); err != nil {
		return err
	}
	*ds = DimmerSet(d)
	return nil
}

type DiscreteLEDStatus struct {
	StatusLights  string
	FlasherStatus LEDSequence
	StroberStatus LEDSequence
}

func (s *DiscreteLEDStatus) clear(lightsInstalled string) {
	s.StatusLights = strings.Repeat("_", len(lightsInstalled))
	s.FlasherStatus.clear()
	s.StroberStatus.clear()
}

func (s *DiscreteLEDStatus) setLights(lights []byte, lightsInstalled string) {
	l := bytes.Repeat([]byte{'_'}, len(lightsInstalled))
	for _, lightToSet := range lights {
		if idx := strings.IndexByte(lightsInstalled, lightToSet); idx >= 0 {
			l[idx] = lightToSet
		} else if lightToSet >= '0' && lightToSet <= '9' && int(lightToSet-'0') < len(l) {
			l[lightToSet-'0'] = lightToSet
		}
	}

	s.StatusLights = string(l)
}

func (s *LEDSequence) clear() {
	s.IsRunning = false
}

func (s *LEDSequence) set(l []byte, up, on, down, off float64) {
	if len(l) == 0 {
		s.clear()
		return
	}
	s.IsRunning = true
	s.Position = 0
	s.Sequence = make(LightList, len(l))
	copy(s.Sequence, l)
	if up == 0.0 && on == 0.0 && down == 0.0 && off == 0.0 {
		s.CustomTiming.Enabled = false
	} else {
		s.CustomTiming.Enabled = true
	}
	s.CustomTiming.Up = up
	s.CustomTiming.Down = down
	s.CustomTiming.On = on
	s.CustomTiming.Off = off
}

type NetworkDriver interface {
	Attach(netID, device string, baudRate, globalAddress int) error
	AllLightsOffBytes([]int, []byte) ([]byte, error)
	Bytes([]int, []byte) ([]byte, error)
	Detach()
	Send(string) error
	SendBytes([]byte) error
	Receive() ([]byte, error)
	IsPortOpen() bool
}

type BaseNetworkDriver struct {
	Port          serial.Port
	isPortOpen    bool
	GlobalAddress int
	NetworkID     string
}

type DirectDriver struct {
	BaseNetworkDriver
}

type RS485Driver struct {
	BaseNetworkDriver
}

type HTTPDriver struct {
	BaseNetworkDriver
	APIEndpoint string
}

func (d *DirectDriver) IsPortOpen() bool {
	return d.isPortOpen
}

func (d *DirectDriver) Attach(netID, device string, baudRate, globalAddress int) error {
	var err error
	if d == nil {
		return fmt.Errorf("attach attempted to nil device %s", netID)
	}
	d.Detach()
	d.NetworkID = netID
	d.GlobalAddress = globalAddress
	if d.Port, err = serial.Open(device, &serial.Mode{BaudRate: baudRate}); err == nil {
		d.isPortOpen = true
		err = d.Port.SetReadTimeout(60 * time.Second)
	}
	return err
}

func (d *DirectDriver) Detach() {
	if d.isPortOpen {
		d.Port.Close()
		d.isPortOpen = false
	}
}

func (d *DirectDriver) AllLightsOffBytes(_ []int, command []byte) ([]byte, error) {
	return append(append([]byte{0x04}, command...), 0x04), nil
}

// Bytes produces a raw byte stream appropriate to send the given command to the target addresses on
// a direct USB network.
//
// Since this is a direct connection, addrs is ignored. We simply send the command string as-is,
// followed by the ^D terminator at the end.
// We do not allow ^D to appear in the command string itself.
// As a special case, if command is the empty string, we send "X^DC^D" to clear the whole sign.
func (d *DirectDriver) Bytes(addrs []int, command []byte) ([]byte, error) {
	if bytes.Contains(command, []byte{'\004'}) {
		return nil, fmt.Errorf("cannot send command with a ^D character in it")
	}
	return append(append([]byte{'\004'}, command...), '\004'), nil
}

func (d *RS485Driver) AllLightsOffBytes(a []int, _ []byte) ([]byte, error) {
	if len(a) == 1 && a[0] < 16 {
		return []byte{byte(0x80 | (a[0] & 0x0f))}, nil
	}
	return nil, fmt.Errorf("alloff cannot target more than one device or devices with ID > 15")
}

// Bytes produces a raw byte stream appropriate to send the given command to the target addresses on
// an RS-485 serial network. Bytes after the initial start-of-command byte are escaped.
//
// For RS-485 network-connected devices, a binary header is sent first:
//
//	1001aaaa                               Single/global target with address in [0,15]
//	1011gggg 00nnnnnn 00aaaa ... 00aaaa    Multiple targets or addresses >15
//	                  \_______n_______/
func (d *RS485Driver) Bytes(a []int, cmd []byte) ([]byte, error) {
	output485 := Escape485(cmd)

	if len(a) == 0 {
		return nil, fmt.Errorf("command with no target device addresses cannot be sent via RS-485")
	}

	if len(a) == 1 && a[0] < 16 {
		return append([]byte{byte(0x90 | (a[0] & 0x0f))}, output485...), nil
	}

	if len(a) > 63 {
		return nil, fmt.Errorf("number of target device addresses exceeds max of 63")
	}

	addrs := make([]byte, 2+len(a))
	addrs[0] = 0xb0 | (byte(d.GlobalAddress) & 0x0f)
	addrs[1] = byte(len(a) & 0x3f)
	for i := 0; i < len(a); i++ {
		if a[i] < 0 || a[i] > 63 {
			return nil, fmt.Errorf("address %d out of range [0,63]", a[i])
		}
		addrs[2+i] = byte(a[i] & 0x3f)
	}
	return append(addrs, output485...), nil
}

func (d *RS485Driver) Attach(netID, device string, baudRate, globalAddress int) error {
	var err error
	if d == nil {
		return fmt.Errorf("attach attempted to nil device %s", netID)
	}
	d.Detach()
	d.NetworkID = netID
	d.GlobalAddress = globalAddress
	if d.Port, err = serial.Open(device, &serial.Mode{BaudRate: baudRate}); err == nil {
		d.isPortOpen = true
		err = d.Port.SetReadTimeout(1 * time.Second)
	}
	return err
}

func (d *RS485Driver) Detach() {
	if d != nil && d.isPortOpen {
		d.Port.Close()
		d.isPortOpen = false
	}
}

func (d *RS485Driver) IsPortOpen() bool {
	return d.isPortOpen
}

// Attach creates an appropriate driver if one doesn't already exist. If there was already an open driver,
// that is closed. Then the port is opened based on the description held in the NetworkDescription fields.
func (net *NetworkDescription) Attach(netID string, globalAddress int) error {
	var err error

	if net.driver == nil {
		switch net.ConnectionType {
		case RS485Network:
			net.driver = &RS485Driver{}
			log.Printf("network %s: RS-485 network", netID)
		case USBDirect:
			net.driver = &DirectDriver{}
			log.Printf("network %s: USB direct connection", netID)
		case HTTPJSON:
			net.driver = &HTTPDriver{}
			log.Printf("network %s: HTTP connection", netID)
			return net.driver.Attach(netID, net.Device, 0, globalAddress)
		default:
			return fmt.Errorf("network %s: invalid network type %v", netID, net.ConnectionType)
		}
	}

tryOpeningPort:
	for {
		if net.Device != "" {
			// hard-coded device name to use
			if err = net.driver.Attach(netID, net.Device, net.BaudRate, globalAddress); err != nil {
				pe, isPortError := err.(*serial.PortError)
				if isPortError && pe.Code() == serial.PortBusy {
					log.Printf("network %s device %s is busy; retrying...", netID, net.Device)
					time.Sleep(250 * time.Millisecond)
					continue
				}
				return fmt.Errorf("can't open network %s serial device %s: %v", netID, net.Device, err)
			}
			return nil
		}

		// not hard-coded; search for a device matching a regular expression
		log.Printf("network %s: searching for available device port in %s...", netID, net.DeviceDir)
		fileList, err := os.ReadDir(net.DeviceDir)
		if err != nil {
			return fmt.Errorf("can't scan directory %s: %v", net.DeviceDir, err)
		}
		for _, f := range fileList {
			if f.IsDir() {
				continue
			}

			ok, err := regexp.MatchString(net.DeviceRegexp, f.Name())
			if err != nil {
				return fmt.Errorf("matching %s vs %s: %v", f.Name(), net.DeviceRegexp, err)
			}
			if ok {
				if err = net.driver.Attach(netID, fmt.Sprintf("%s%c%s", net.DeviceDir, os.PathSeparator, f.Name()), net.BaudRate, globalAddress); err != nil {
					pe, isPortError := err.(*serial.PortError)
					if isPortError && pe.Code() == serial.PortBusy {
						log.Printf("network %s: found busy device %s; waiting for it to be free...", netID, f.Name())
						time.Sleep(250 * time.Millisecond)
						continue tryOpeningPort
					}
					return fmt.Errorf("can't open network %s serial device %s: %v", netID, f.Name(), err)
				}
				log.Printf("network %s: opened %s%c%s", netID, net.DeviceDir, os.PathSeparator, f.Name())
				return nil
			}
		}
		return fmt.Errorf("network %s: unable to open any device matching /%s/ in %s.", netID, net.DeviceRegexp, net.DeviceDir)
	}
}

func showAddress(a byte) string {
	if a == 0xff {
		return "--"
	}
	return fmt.Sprintf("%d", a)
}

func logDimmers(d []DimmerSet, ok []bool) {
	var s string
	for i, v := range d {
		if ok[i] {
			s += fmt.Sprintf("#%d=%02X ", i, v)
		} else {
			s += fmt.Sprintf("#%d -- ", i)
		}
	}

	log.Printf("| dimmers: %s", s)
}

func logStatusLEDs(s DiscreteLEDStatus) {
	log.Printf("| status lights on=%s", s.StatusLights)
	if s.FlasherStatus.IsRunning {
		if s.FlasherStatus.CustomTiming.Enabled {
			log.Printf("| custom timing (sec): up=%v, on=%v, down=%v, off=%v",
				s.FlasherStatus.CustomTiming.Up,
				s.FlasherStatus.CustomTiming.On,
				s.FlasherStatus.CustomTiming.Down,
				s.FlasherStatus.CustomTiming.Off,
			)
		}
		log.Printf("| flasher running, pos=%d, sequence=%s", s.FlasherStatus.Position, s.FlasherStatus.Sequence)
	} else {
		log.Printf("| flasher stopped")
	}
	if s.StroberStatus.IsRunning {
		log.Printf("| strober running, pos=%d, sequence=%s", s.StroberStatus.Position, s.StroberStatus.Sequence)
	} else {
		log.Printf("| strober stopped")
	}
}

func ProbeDevices(configData *ConfigData) error {
	for nid, net := range configData.Networks {
		if net.driver == nil {
			log.Printf("Network id %s: type=%v; dev=%s; dir=%s; regexp=%s; speed=%d; open=NIL", nid, net.ConnectionType, net.Device, net.DeviceDir, net.DeviceRegexp, net.BaudRate)
		} else {
			log.Printf("Network id %s: type=%v; dev=%s; dir=%s; regexp=%s; speed=%d; open=%v", nid, net.ConnectionType, net.Device, net.DeviceDir, net.DeviceRegexp, net.BaudRate, net.driver.IsPortOpen())
		}
	}
	for id, dev := range configData.Devices {
		var commands, rawBytes []byte
		var received []byte
		var err error
		log.Printf("Device address %d: type=%v; net=%s (%s; s/n=%s)", id, dev.DeviceType, dev.NetworkID, dev.Description, dev.Serial)
		if net, ok := configData.Networks[dev.NetworkID]; ok {
			if httpDriver, isValid := net.driver.(*HTTPDriver); isValid && IsWLEDModel(dev.DeviceType) {
				info, err := httpDriver.WLEDQuery()
				if err != nil {
					return err
				}
				log.Printf("probed device ID %d on network %s:", id, dev.NetworkID)
				log.Printf("| WLED module %s version %s build %v, %d LEDs, %d FPS", info.Name,
					info.Version, info.BuildID, info.LEDS.Count, info.LEDS.FramesPerSecond)
				log.Printf("| Max power budget %d mA, max segments %d", info.LEDS.MaxPowerBudget, info.LEDS.MaxSegments)
				log.Printf("| UDP port %d, live? %v; BSSID %s, signal %d, chan %d",
					info.UDPPort, info.IsLive, info.WiFi.BSSID, info.WiFi.Signal, info.WiFi.Channel)
				log.Printf("| Palettes %d, custom %d, user %d, max custom %d, names %v",
					info.Palettes, info.CustomPalettes, info.UserPalettes, info.MaxCustomPalettes, info.UserPaletteNames)
				log.Printf("| Arch %s, core %s; free heap %d, uptime %d", info.Arch, info.Core, info.FreeHeap, info.Uptime)
				log.Printf("| Brand %s, product %s; MAC %s, IP %s; device ID %s",
					info.Brand, info.Product, info.MACAddr, info.IPAddr, info.DeviceID)
				if dev.Serial != info.DeviceID {
					log.Printf("| WARNING! device serial number is %s but configured as %s!", info.DeviceID, dev.Serial)
				}
				continue
			}

			sender, parser := Query()
			if commands, err = sender(nil, dev.DeviceType, nil, configData); err != nil {
				return fmt.Errorf("error getting bytestream for unit %d: %v", id, err)
			}

			if rawBytes, err = net.driver.Bytes([]int{id}, commands); err != nil {
				return fmt.Errorf("error preparing bytestream for unit %d: %v", id, err)
			}

			if err := func() error {
				lockNetwork(dev.NetworkID)
				defer unlockNetwork(dev.NetworkID)

				if err := net.driver.SendBytes(rawBytes); err != nil {
					return fmt.Errorf("error transmitting bytestream to unit %d: %v", id, err)
				}

				if received, err = net.driver.Receive(); err != nil {
					return fmt.Errorf("device address %d: error reading reply: %v", id, err)
				}
				return nil
			}(); err != nil {
				return err
			}

			if statData, err := parser(dev.DeviceType, received); err == nil {
				if s, ok := statData.(DeviceStatus); ok {
					log.Printf("probed device ID %d on network %s:", id, dev.NetworkID)
					switch s.DeviceModelClass {
					case 'B':
						switch dev.DeviceType {
						case Busylight1:
							log.Printf("| busylight model 1.x; USB speed %d; %s EEPROM; sound %s; hw %s; fw %s; S/N %s",
								s.SpeedUSB, EEPROMTypeName(s.EEPROM), SoundTypeName(s.Sound), s.HardwareRevision, s.FirmwareRevision, s.Serial)
							logDimmers(s.Dimmers, s.DimmerValid)
							logStatusLEDs(s.StatusLEDs)
						case Busylight2:
							log.Printf("| busylight model 2.x; address %v; global %v; USB speed %d; RS-485 speed %d; %s EEPROM; sound %s; hw %s; fw %s; S/N %s",
								showAddress(s.DeviceAddress), showAddress(s.GlobalAddress), s.SpeedUSB, s.Speed485, EEPROMTypeName(s.EEPROM), SoundTypeName(s.Sound), s.HardwareRevision, s.FirmwareRevision, s.Serial)
							logDimmers(s.Dimmers, s.DimmerValid)
							logStatusLEDs(s.StatusLEDs)
						default:
							log.Printf("| IDENTIFIES AS A BUSYLIGHT DEVICE REV %s BUT CONFIGURED AS %s!", s.HardwareRevision, HardwareModelName(dev.DeviceType))
						}
					case 'M':
						switch dev.DeviceType {
						case Readerboard3Mono:
							log.Printf("| monochrome %dx%d readerboard model 3.x; address %v; global %v; USB speed %d; RS-485 speed %d; %s EEPROM; sound %s; hw %s; fw %s; S/N %s",
								s.MatrixWidth, s.MatrixHeight,
								showAddress(s.DeviceAddress), showAddress(s.GlobalAddress), s.SpeedUSB, s.Speed485, EEPROMTypeName(s.EEPROM), SoundTypeName(s.Sound), s.HardwareRevision, s.FirmwareRevision, s.Serial)
							logDimmers(s.Dimmers, s.DimmerValid)
							logStatusLEDs(s.StatusLEDs)
							log.Printf("| bitmap %s", hex.EncodeToString(s.ImageBitmap[0][:]))
							log.Printf("| flash  %s", hex.EncodeToString(s.ImageBitmap[1][:]))
							logMonochromeBitmap(s.ImageBitmap)
						default:
							log.Printf("| IDENTIFIES AS A MONOCHROME READERBOARD DEVICE REV %s BUT CONFIGURED AS %s!", s.HardwareRevision, HardwareModelName(dev.DeviceType))
						}
					case 'C':
						switch dev.DeviceType {
						case Readerboard3RGB:
							log.Printf("| color %dx%d readerboard model 3.x; address %v; global %v; USB speed %d; RS-485 speed %d; %s EEPROM; sound %s; hw %s; fw %s; S/N %s",
								s.MatrixWidth, s.MatrixHeight,
								showAddress(s.DeviceAddress), showAddress(s.GlobalAddress), s.SpeedUSB, s.Speed485, EEPROMTypeName(s.EEPROM), SoundTypeName(s.Sound), s.HardwareRevision, s.FirmwareRevision, s.Serial)
							logDimmers(s.Dimmers, s.DimmerValid)
							logStatusLEDs(s.StatusLEDs)
							log.Printf("| red plane %s", hex.EncodeToString(s.ImageBitmap[0][:]))
							log.Printf("| green \"   %s", hex.EncodeToString(s.ImageBitmap[1][:]))
							log.Printf("| blue  \"   %s", hex.EncodeToString(s.ImageBitmap[2][:]))
							log.Printf("| flash \"   %s", hex.EncodeToString(s.ImageBitmap[3][:]))
							logColorBitmap(s.ImageBitmap)
						default:
							log.Printf("| IDENTIFIES AS A COLOR READERBOARD DEVICE REV %s BUT CONFIGURED AS %s!", s.HardwareRevision, HardwareModelName(dev.DeviceType))
						}

					default:
						log.Printf("| legacy or unknown device; raw data %s", received)
					}

					if s.DeviceAddress != 0xff && s.DeviceAddress != byte(id) {
						log.Printf("| WARNING! device thinks its address is %d but configured as %d!", s.DeviceAddress, id)
					}
					if s.GlobalAddress != byte(configData.GlobalAddress) {
						log.Printf("| WARNING! device thinks the global address is %d but configured as %d!", s.GlobalAddress, configData.GlobalAddress)
					}
					if s.Serial != dev.Serial {
						log.Printf("| WARNING! device serial number is %s but configured as %s!", s.Serial, dev.Serial)
					}
				} else {
					log.Printf("device address %d: unable to parse raw response %s", id, received)
				}
			} else {
				return fmt.Errorf("device address %d: error parsing output \"%s\": %v", id, received, err)
			}
		} else {
			return fmt.Errorf("device address %d: belongs to network %s but I can't find that network.", id, dev.NetworkID)
		}
	}
	return nil
}

func logColorBitmap(bitmap [][64]byte) {
	for row := 0; row < 8; row++ {
		var s strings.Builder
		for col := 0; col < 64; col++ {
			var bits byte
			if bitmap[0][col]&(1<<row) != 0 {
				bits |= 0x01
			}
			if bitmap[1][col]&(1<<row) != 0 {
				bits |= 0x02
			}
			if bitmap[2][col]&(1<<row) != 0 {
				bits |= 0x04
			}
			if bitmap[3][col]&(1<<row) != 0 {
				bits |= 0x08
			}
			switch bits {
			case 0:
				fmt.Fprintf(&s, ".")
			case 1:
				fmt.Fprintf(&s, "R")
			case 2:
				fmt.Fprintf(&s, "G")
			case 3:
				fmt.Fprintf(&s, "Y")
			case 4:
				fmt.Fprintf(&s, "B")
			case 5:
				fmt.Fprintf(&s, "M")
			case 6:
				fmt.Fprintf(&s, "C")
			case 7:
				fmt.Fprintf(&s, "W")
			case 8:
				fmt.Fprintf(&s, "!")
			case 9:
				fmt.Fprintf(&s, "r")
			case 10:
				fmt.Fprintf(&s, "g")
			case 11:
				fmt.Fprintf(&s, "y")
			case 12:
				fmt.Fprintf(&s, "b")
			case 13:
				fmt.Fprintf(&s, "m")
			case 14:
				fmt.Fprintf(&s, "c")
			case 15:
				fmt.Fprintf(&s, "w")
			default:
				fmt.Fprintf(&s, "?(%02X)", bits)
			}
		}
		log.Printf("| %s |", s.String())
	}
}

func logMonochromeBitmap(bitmap [][64]byte) {
	for row := 0; row < 8; row++ {
		var s strings.Builder
		for col := 0; col < 64; col++ {
			var bits byte
			if bitmap[0][col]&(1<<row) != 0 {
				bits |= 0x01
			}
			if bitmap[1][col]&(1<<row) != 0 {
				bits |= 0x08
			}
			switch bits {
			case 0:
				fmt.Fprintf(&s, ".")
			case 1:
				fmt.Fprintf(&s, "@")
			case 8:
				fmt.Fprintf(&s, "!")
			case 9:
				fmt.Fprintf(&s, "#")
			default:
				fmt.Fprintf(&s, "?")
			}
		}
		log.Printf("| %s |", s.String())
	}
}

func AttachToAllNetworks(configData *ConfigData) error {
	for netID, net := range configData.Networks {
		if err := net.Attach(netID, configData.GlobalAddress); err != nil {
			return err
		}
		configData.Networks[netID] = net
	}
	return nil
}

// Detach closes the hardware connection to the network interface associated with
// this network, if there was one.
func (net *NetworkDescription) Detach() {
	if net != nil && net.driver != nil {
		net.driver.Detach()
	}
}

// Send sends a command string to a device.
func (d *DirectDriver) Send(command string) error {
	return d.SendBytes([]byte(command))
}

func (d *RS485Driver) Send(command string) error {
	return d.SendBytes([]byte(command))
}

func (d *DirectDriver) SendBytes(command []byte) error {
	pf1 := fmt.Sprintf("-> (USB %s) ", d.NetworkID)
	pf2 := fmt.Sprintf("        %*s  ", len(d.NetworkID), "")
	for _, s := range util.LineWrap(util.Hexdump(command, util.WithoutNewline), pf1+"[", pf1+"\u250c", pf2+"\u2502", pf2+"\u2514") {
		log.Print(s)
	}
	if _, err := d.Port.Write(command); err != nil {
		log.Printf("%v", err)
		return err
	}
	return d.Port.Drain()
}

func (d *RS485Driver) SendBytes(command []byte) error {
	pf1 := fmt.Sprintf("-> (485 %s) ", d.NetworkID)
	pf2 := fmt.Sprintf("        %*s  ", len(d.NetworkID), "")
	for _, s := range util.LineWrap(util.Hexdump(command, util.WithoutNewline), pf1+"[", pf1+"\u250c", pf2+"\u2502", pf2+"\u2514") {
		log.Print(s)
	}
	if _, err := d.Port.Write(command); err != nil {
		return err
	}
	return d.Port.Drain()
}

const maxResponseLength = 1024

func (d *DirectDriver) Receive() ([]byte, error) {
	inputbuf := make([]byte, maxResponseLength)
	var rawResponse []byte

	for {
		bytesRead, err := d.Port.Read(inputbuf)
		if err != nil {
			return nil, err
		}
		if bytesRead == 0 {
			return nil, fmt.Errorf("EOF reading from device")
		}

		for i := 0; i < bytesRead; i++ {
			if len(rawResponse) >= maxResponseLength {
				return nil, fmt.Errorf("read more than max %d bytes from device", maxResponseLength)
			}
			if inputbuf[i] == '\n' {
				return rawResponse, nil
			}
			rawResponse = append(rawResponse, inputbuf[i])
		}
	}
}

func (d *RS485Driver) Receive() ([]byte, error) {
	return nil, fmt.Errorf("receive not implemented")
}

//  _   _ _____ _____ ____
// | | | |_   _|_   _|  _ \
// | |_| | | |   | | | |_) |
// |  _  | | |   | | |  __/
// |_| |_| |_|   |_| |_|
//
// HTTP network devices talk to units that are on the network with their own IP addresses
// which we access via an HTTP endpoint. Currently we assume this means a JSON API we
// can post to there.
//
// The configuration fields are used in this way instead of the usual way they're
// used for serial devices (USB or RS-485):
//
//   Port               Unused
//	 isPortOpen			true if driver can be used (doens't mean we have an active connection)
//   GlobalAddress      (usual meaning)
//   NetworkID          (usual meaning)
//   APIEndpoint		url up to API command part
//
// We don't necessarily keep a connection alive to the device, so we fake the whole
// "attach" and "isPortOpen" semantic here, and just initiate POST operations each time
// we want to send a command. The underlying library calls MAY keep the TCP connectdion alive
// between these.
//

// Attach for HTTP devices uses the device parameter to specify the API Endpoint value.
// This is stored for later transactions. We don't actually try to initiate any contact with
// the device at this point. We'll talk to it when we have traffic to send to it.
func (d *HTTPDriver) Attach(netID, device string, _, globalAddress int) error {
	if d == nil {
		return fmt.Errorf("attach attempted to nil device %s", netID)
	}

	log.Printf("HTTP network %s attach for device endpoint %s; global address %d", netID, device, globalAddress)
	d.isPortOpen = true
	d.GlobalAddress = globalAddress
	d.NetworkID = netID
	d.APIEndpoint = device
	return nil
}

// Detach for HTTP devices just clears the structure and flags that we shouldn't use this device anymore.
func (d *HTTPDriver) Detach() {
	log.Printf("HTTP network %s detach", d.NetworkID)
	d.isPortOpen = false
	d.APIEndpoint = ""
}

// AllLightsOffBytes should not be used for HTTP devices since, unlike the serial devices
// there's no difference in how the command streams are put together depending on
// whether they are sent via RS-485 or USB serial. This is just network traffic, so the
// data encoding is at the device driver level, not here.
func (d *HTTPDriver) AllLightsOffBytes([]int, []byte) ([]byte, error) {
	return nil, fmt.Errorf("inappropriate call to AllLightsOffBytes for HTTPDriver device on network %v", d.NetworkID)
}

// Bytes should not be used for HTTP devices since, unlike the serial devices
// there's no difference in how the command streams are put together depending on
// whether they are sent via RS-485 or USB serial. This is just network traffic, so the
// data encoding is at the device driver level, not here.
func (d *HTTPDriver) Bytes([]int, []byte) ([]byte, error) {
	return nil, fmt.Errorf("inappropriate call to Bytes for HTTPDriver device on network %v", d.NetworkID)
}

// Send should not be used for HTTP devices since, unlike the serial devices
// there's no difference in how the command streams are put together depending on
// whether they are sent via RS-485 or USB serial. This is just network traffic, so the
// data encoding is at the device driver level, not here.
func (d *HTTPDriver) Send(cmd string) error {
	return fmt.Errorf("inappropriate call to Send for HTTPDriver device on network %v", d.NetworkID)
}

// CommandAndResponse is the basic communication method for HTTP devices. It takes a byte slice
// JSON message body, POSTS it to an API endpoint constructed with apiCmd as the final piece from
// the configured device endpoint URL string, and returns the JSON response from the device
// as a byte slice value.
func (d *HTTPDriver) CommandAndResponse(apiCmd string, cmd []byte) ([]byte, error) {
	var response *http.Response
	var err error

	if cmd == nil {
		response, err = http.Get(d.APIEndpoint + apiCmd)
	} else {
		response, err = http.Post(d.APIEndpoint+apiCmd, "application/json", bytes.NewBuffer(cmd))
	}
	defer func() {
		if err := response.Body.Close(); err != nil {
			log.Printf("Error closing HTTP response: %v", err)
		}
	}()
	if err != nil {
		log.Printf("HTTP post %s error %v", string(cmd), err)
		return nil, err
	} else {
		log.Printf("HTTP post %s %s", string(cmd), response.Status)

		data, err := io.ReadAll(response.Body)
		if err != nil {
			log.Printf("HTTP post %s error reading response: %v", string(cmd), err)
			return nil, err
		}
		return data, nil
	}
}

func (d *HTTPDriver) WLEDQuery() (WLEDInfo, error) {
	var info WLEDInfo
	data, err := d.CommandAndResponse("info", nil)
	if err != nil {
		return WLEDInfo{}, fmt.Errorf("error retrieving response from WLED unit %s: %v", d.NetworkID, err)
	}
	if err := json.Unmarshal(data, &info); err != nil {
		return WLEDInfo{}, fmt.Errorf("error unmarshaling JSON response from WLED unit %s: %v", d.NetworkID, err)
	}
	return info, nil
}

func (d *HTTPDriver) WLEDState(s WLEDState) error {
	requestData, err := json.Marshal(s)
	if err != nil {
		return fmt.Errorf("error marshaling WLED unit %s request JSON %v: %v", d.NetworkID, s, err)
	}
	_, err = d.CommandAndResponse("state", requestData)
	if err != nil {
		return fmt.Errorf("error communicating with unit %s: %v", d.NetworkID, err)
	}
	return nil
}

func (d *HTTPDriver) WLEDCustomState(s any) error {
	requestData, err := json.Marshal(s)
	if err != nil {
		return fmt.Errorf("error marshaling WLED unit %s request JSON %v: %v", d.NetworkID, s, err)
	}
	_, err = d.CommandAndResponse("state", requestData)
	if err != nil {
		return fmt.Errorf("error communicating with unit %s: %v", d.NetworkID, err)
	}
	return nil
}

// SendBytes should not be used for HTTP devices since, unlike the serial devices
// there's no difference in how the command streams are put together depending on
// whether they are sent via RS-485 or USB serial. This is just network traffic, so the
// data encoding is at the device driver level, not here.
func (d *HTTPDriver) SendBytes(cmd []byte) error {
	return fmt.Errorf("inappropriate call to SendBytes for HTTPDriver device on network %v", d.NetworkID)
}

// Receive should not be used for HTTP devices since, unlike the serial devices
// there's no difference in how the command streams are put together depending on
// whether they are sent via RS-485 or USB serial. This is just network traffic, so the
// data encoding is at the device driver level, not here.
func (d *HTTPDriver) Receive() ([]byte, error) {
	return nil, fmt.Errorf("inappropriate call to Receive for HTTPDriver device on network %v", d.NetworkID)
}

func (d *HTTPDriver) IsPortOpen() bool {
	return d.isPortOpen
}

func parseLightList(lights []byte) ([]byte, error) {
	return lights, nil
}

func parseLEDSequence(packet []byte) (LEDSequence, error) {
	var seq LEDSequence
	var err error

	if len(packet) < 2 {
		return seq, fmt.Errorf("malformed LED sequence (short)")
	}
	if packet[0] == 'R' {
		seq.IsRunning = true
	} else if packet[0] != 'S' {
		return seq, fmt.Errorf("malformed LED sequence (LED status run state code %v not defined)", packet[0])
	}

	if packet[1] == '_' {
		return seq, nil
	}

	if len(packet) < 3 || packet[2] != '@' {
		return seq, fmt.Errorf("malformed LED sequence (short or missing @)")
	}

	if seq.Position, err = parsePos(packet[1]); err != nil {
		return seq, err
	}

	seq.Sequence = []byte(packet[3:])
	return seq, nil
}

func parsePos(code byte) (int, error) {
	if code == '~' {
		return -1, nil
	}
	if code < 48 || code > 48+63 {
		return 0, fmt.Errorf("code %d out of range ['0','o']", code)
	}
	return int(code) - 48, nil
}

// WLED State Object
type WLEDState struct {
	IsOn           any  `json:"on,omitempty"`         // bool or string with bool-ish value: on/off state of the light
	Brightness     int  `json:"bri,omitempty"`        // [0..255] use IsOn=false to turn light off in preference to Brightness=0
	Transition     int  `json:"transition,omitempty"` // [0..65535] crossfade duration (x100mS)
	APITransition  int  `json:"tt,omitempty"`         // [0..65535] crossfade duration (x100mS) (Current API call only)
	PresetID       any  `json:"ps,omitempty"`         // [-1..250] ID of currently set preset "1~17~" iterates 1-17, "4~10r" selects random between 4-10 inclusive
	SavePreset     int  `json:"psave,omitempty"`      // [1..250] save current config to preset slot
	SaveBounds     bool `json:"sb,omitempty"`         // Save start and stop with SavePreset
	SaveBrightness bool `json:"ib,omitempty"`         // Save brightness with SavePreset
	SaveSegments   bool `json:"sc,omitempty"`         // Save selected segments with SavePreset
	PlaylistID     int  `json:"pl,omitempty"`         // [-1..250] ID of currently set playlist (r/o)
	DeletePlaylist int  `json:"pdel,omitempty"`       // [1..250] ID playlist to delete
	NightLight     struct {
		IsActive          bool `json:"on,omitempty"`   // nightlight currently active
		Duration          int  `json:"dur,omitempty"`  // [1..255] duration in minutes
		Mode              int  `json:"mode,omitempty"` // [0..3] nightlight mode 0=instant 1=fade 2=color fade 3=sunrise
		TargetBrightness  int  `json:"tbri,omitempty"` // [0..255] target brightness
		RemainingDuration int  `json:"rem,omitempty"`  // [-1..15300] remaining time in seconds or -1 if not active (r/o)
	} `json:"nl,omitempty"`
	UDP struct {
		Send      bool `json:"send"`         // send broadcast UDP sync on state change
		Recv      bool `json:"recv"`         // reception enabled
		SendGroup int  `json:"sgrp"`         // [0..255]
		RecvGroup int  `json:"rgrp"`         // [0..255]
		DontSend  bool `json:"nn,omitempty"` // don't send broadcast packet for this API call only
	} `json:"udpn,omitempty"`
	Verbose          bool          `json:"v,omitempty"`       // respond with verbose state object
	RebootNow        bool          `json:"rb,omitempty"`      // reboot device immediately
	LiveMode         bool          `json:"live,omitempty"`    // go into (or exit) live streaming mode and blank LEDs	TODO omitempty may be an issue with this
	LiveDataOverride int           `json:"lor,omitempty"`     // live data override 0=off, 1=override until live data ends 2=until reboot
	SetTime          uint32        `json:"time,omitempty"`    // set module time to unix timestamp.
	MainSegmentID    int           `json:"mainseg,omitempty"` // [0..info.leds.maxseg-1] set ID of the main segment
	Segments         []WLEDSegment `json:"seg,omitempty"`
	PlayList         struct {
		PresetIDs  []int `json:"ps"`                   // preset IDs to cycle through (up to 100 entries)
		Duration   []int `json:"dur"`                  // duration each preset stays active (x.10S)
		Transition any   `json:"transition,omitempty"` // array of crossfade times (x.10S) or single value or omitted (for default)
		Repeat     int   `json:"repeat"`               // times to repeat the cycle or 0 for infinite looping
		End        int   `json:"end"`                  // final preset ID to load after the preset is done 0=stay on last, 255 restore one before playlist started
		Shuffle    bool  `json:"r,omitempty"`          // randomize preset order?
	} `json:"playlist,omitempty"`
	TimeBase          uint32 `json:"tb,omitempty"`
	LoadMap           int    `json:"ledmap,omitempty"` // [0..9]
	RemoveLastPalette bool   `json:"mcpal,omitempty"`
	AdvanceNextPreset bool   `json:"np,omitempty"`
}

// WLED info
type WLEDInfo struct {
	Version string `json:"ver"`
	BuildID uint32 `json:"vid"`
	LEDS    struct {
		Count           int  `json:"count"` // total LEDs
		FramesPerSecond int  `json:"fps"`   // 0-255
		IsRGBW          bool `json:"rgbw"`
		//		DisplayWhiteChannelSlider bool `json:"wv"`
		PowerUsage     int  `json:"pwr"`    // milliamps 0-65000
		MaxPowerBudget int  `json:"maxpwr"` // milliamps 0-65000
		MaxSegments    byte `json:"maxseg"`
		Capabilities   byte `json:"lc"` // logical AND of capabilities of segments
	} `json:"leds"`
	ToggleReceiveSend  bool     `json:"str"` // use single button for receive+send
	Name               string   `json:"name"`
	UDPPort            uint16   `json:"udpport"`
	IsLive             bool     `json:"live"` // receiving live in realtime via UDP?
	RealtimeDataSource string   `json:"lm"`
	WebSockets         int      `json:"ws"` // -1-8 -1 means unsupported
	FXCount            byte     `json:"fxcount"`
	Palettes           uint16   `json:"palcount"`
	CustomPalettes     byte     `json:"cpalcount"` // counts down from highest custom ID
	UserPalettes       byte     `json:"umpalcount"`
	MaxCustomPalettes  byte     `json:"cpalmax"`
	UserPaletteNames   []string `json:"umpalnames"`
	WiFi               struct {
		BSSID   string `json:"bssid"`
		Signal  int    `json:"signal"`  // 0-100
		Channel int    `json:"channel"` // 1-14
	} `json:"wifi"`
	LittleFileSystem struct {
		UsedSpace     uint32 `json:"u"`   // kbytes
		TotalSize     uint32 `json:"t"`   // kbytes
		PresetModTime uint32 `json:"pmt"` // unix timestamp for presets.json
	} `json:"fs"`
	OtherDevices int    `json:"ndc"` // number of others discovered; -1 if disabled
	Arch         string `json:"arch"`
	Core         string `json:"core"`
	LwIPVersion  int    `json:"lwip"` // 1 or 2 on ESP8266, 0 N/A on ESP32, DEPRECATED
	FreeHeap     uint32 `json:"freeheap"`
	Uptime       uint32 `json:"uptime"` //time since last boot in seconds
	Opt          uint16 `json:"opt"`
	Brand        string `json:"brand"`
	Product      string `json:"product"`
	MACAddr      string `json:"mac"`
	IPAddr       string `json:"ip"`
	DeviceID     string `json:"deviceid"`
	PSRamSize    uint32 `json:"psrSz"`
	PSRamFree    uint32 `json:"psram"`
	Repository   string `json:"repo"`
}

// WLED Segment
type WLEDSegment struct {
	ID                   int    `json:"id,omitempty"`     // [0..info.maxseg-1] 0-indexed ID of the segment (inferred if omitted)
	Start                int    `json:"start,omitempty"`  // [0..info.leds.count-1] starting LED for this segment
	Stop                 int    `json:"stop,omitempty"`   // [0..info.leds.count] ending LED for this segment (exclusive)
	StartY               int    `json:"startY,omitempty"` // [0..matrix width] start row from top-left corner
	StopY                int    `json:"stopY,omitempty"`  // [1..matrix height] start row from top-left corner
	Len                  int    `json:"len,omitempty"`    // [0..info.leds.count] stop-start. If Stop is given, Len is ignored.
	Group                int    `json:"grp,omitempty"`    // [0..255] grouping (number of consecutive LEDs of same segment to be same color)
	Spacing              int    `json:"spc,omitempty"`    // [0..255] spacing (how many "off" LEDs between each group)
	Offset               int    `json:"of,omitempty"`     // [-len+1..len] offset (how many LEDs to rotate virtual start)
	Colors               []any  `json:"col,omitempty"`    // list of individual LED colors to set; each is [r,g,b] or [r,g,b,w] where the values are integers 0..255; or single hex strings as "rrggbb" or literal string "r" for a random color
	FXID                 any    `json:"fx,omitempty"`     // [0..info.fxcount-1] ID of effect or "r" for random, "~" to increment or "~-" to decrement
	FXSpeed              any    `json:"sx,omitempty"`     // [0..255] relative FX speed or "~" to increment "~-" decrement "~10" inc by 10, "~-10" dec by 10
	FXIntensity          any    `json:"ix,omitempty"`     // [0..255] FX intensity or "~" to increment "~-" decrement "~10" inc by 10, "~-10" dec by 10
	FXSlider1            int    `json:"c1,omitempty"`     // [0..255] FX custom slider 1
	FXSlider2            int    `json:"c2,omitempty"`     // [0..255] FX custom slider 2
	FXSlider3            int    `json:"c3,omitempty"`     // [0..255] FX custom slider 3
	FXOption1            bool   `json:"o1,omitempty"`     // FX option 1
	FXOption2            bool   `json:"o2,omitempty"`     // FX option 2
	FXOption3            bool   `json:"o3,omitempty"`     // FX option 3
	PaletteID            any    `json:"pal,omitempty"`    // [0..255] color palette ID or "~" to increment "~-" decrement "r" random
	IsSelected           bool   `json:"sel,omitempty"`    // is segment selected?
	IsReversedHorizontal bool   `json:"rev,omitempty"`    // is segment flipped (reverse direction for animations)?
	IsReversedVertical   bool   `json:"rY,omitempty"`     // is segment flipped vertically (reverse direction for animations)?
	IsOn                 bool   `json:"on,omitempty"`     // is segment turned on?
	Brightness           int    `json:"bri,omitempty"`    // [0..255] segment brightness
	IsMirroredHorizontal bool   `json:"mi,omitempty"`     // is segment mirrored horizontally
	IsMirroredVertical   bool   `json:"mY,omitempty"`     // is segment mirrored vertically
	IsTransposed         bool   `json:"tp,omitempty"`     // is segment transposed (swap X and Y dimensions)
	ColorTemperature     int    `json:"cct,omitempty"`    // [0..255 or 1900..10091] white spectrum color temperature
	LoxonePrimary        uint64 `json:"lx,omitempty"`     // [0..100100100] encoded Loxone RGB primary color percentage values in decimal as BBBGGGRRR
	LoxoneSecondary      uint64 `json:"ly,omitempty"`     // [0..100100100 or 200002700..201006500] encoded Loxone RGB secondary color percentage values in decimal as BBBGGGRRR or brightness and temp as 20bbbtttt
	LEDs                 []any  `json:"i,omitempty"`      // individual LEDs to set values (encoded as for Colors field)
	FreezeEffect         bool   `json:"frz,omitempty"`    // freeze/unfreeze current effect
	Expand1DFX           int    `json:"m12,omitempty"`    // setting of segment field Expand 1D FX 0=pixels 1=bar 2=arc 3=corner
	SoundSimulation      int    `json:"si,omitempty"`     // setting of sound simulation type 0=BeatSin 1=WeWillRockYou 2=10_3 3=14_3
	ForceLoadDefaults    bool   `json:"fxdef,omitempty"`  // force loading of effect defaults
	RepeatSettings       bool   `json:"rpt,omitempty"`    // repeat current segment settings
}

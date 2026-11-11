# Mediatek Upstream SoftMAC WiFi Driver - MT76 Release Note (OpenWrt 25.12/Kernel 6.12)

## Compile Environment Requirement

- Minimum requirement: Ubuntu 22.04

##### Toolchain

- Installs essential development tools and libraries, including compilers, build tools. Please refer to https://openwrt.org/docs/guide-developer/toolchain/install-buildsystem for more detail
```
sudo apt update
sudo apt install build-essential clang flex bison g++ gawk \
gcc-multilib g++-multilib gettext git libncurses-dev libssl-dev \
python3-distutils python3-setuptools rsync swig unzip zlib1g-dev file wget
```
---

## Uboot & ATF
The OpenWrt/25.12 or trunk image type is ITB, which cannot be loaded if the original U-Boot is too old. Please update to a newer U-Boot that supports OpenWrt 21.0x, OpenWrt 24.xx, and OpenWrt 25.12 image types.

There are two ways to update Uboot and ATF:
- GitHub: please refer to [Bootloader-Preparation-and-flashing](https://github.com/mediatek/mtk-openwrt-feeds/blob/main/autobuild/unified/doc/MediaTek_OpenWrt_2512_User_Guide.md#bootloader-preparation-and-flashing)
- Tarball: please log in to the DCC or contact the corresponding window to obtain the latest U-Boot and ATF source code.

Also need DTS overlay to enable 10G ethernet for one-time uboot console setting. "0. U-Boot console"

- Filogic880/860 (MT7988) - GMAC1 and GMAC2 is AQR113C 10G PHY case
```
setenv bootconf mt7988a-rfb-spim-nand-nmbm
setenv bootconf_extra mt7988a-rfb-eth1-aqr#mt7988a-rfb-eth2-aqr
saveenv
```

- Filogic880/860 (MT7988) - GMAC1 is Mediatek Internal 2.5G PHY and GMAC2 is AQR113C 10G PHY case
```
setenv bootconf mt7988a-rfb-spim-nand-nmbm
setenv bootconf_extra mt7988a-rfb-eth1-i2p5g-phy#mt7988a-rfb-eth2-aqr
saveenv
```

- Filogic850 (MT7987) - GMAC1 is AN8855 switch and GMAC2 is Mediatek Internal 2.5G PHY and GMAC3 is Mediatek External 2.5G PHY case
    ```
    setenv bootconf mt7987-spim-nand-nmbm
    setenv bootconf_extra mt7987-netsys-eth0-an8855#mt7987-netsys-eth1-i2p5g#mt7987-netsys-eth2-e2p5g
    saveenv
    ```

##### SPIM-NAND
Note: Please follow the SOP below to upgrade the bl2 and fip on your board.

1. Upgrade the bl2.img file according to the DRAM type, and program this file using the "3. Upgrade ATF BL2" option in the U-Boot menu.

2. Upgrade the flp.bin file, and program this file using the "4. Upgrade ATF FIP" option in the U-Boot menu.

3. Reboot the board. You can now upgrade the ITB image using "2. Upgrade firmware"

##### EMMC
Note: Please follow the SOP below to upgrade the U-Boot image and GPT partition on your board.

1. Upgrade u-boot_1g.bin, and program this file using the "6. Upgrade bootloader only" option in the U-Boot menu.

2. Program GPT_EMMC_mt798x_itb using the "7. Upgrade partition table" option in the U-Boot menu.

3. Reboot the board. You can now upgrade the ITB image using "2. Upgrade firmware"
---

### Supported Chipsets
- Filogic8800 802.11a/b/g/n/ac/ax/be/bn BN19000 2.4/5G/6GHz PCIe Chip
- Filogic880/Filogic680/MT7996 802.11a/b/g/n/ac/ax/be BE19000/BE14000 2.4/5G/6GHz PCIe Chip
- Filogic860/Filogic660/MT7992 802.11a/b/g/n/ac/ax/be BE7200/BE5000 2.4/5G PCIe Chip
- Filogic850/Filogic660/MT7992 802.11a/b/g/n/ac/ax/be BE7200/BE5000 2.4/5G PCIe Chip
- Filogic850/Filogic650/MT7990 802.11a/b/g/n/ac/ax/be BE3600 2.4/5G PCIe Chip
---

### Default EEPROM Bin
- Filogic8800/BN19000 (4-4-4)
  - eFEM: mt7999_eeprom.bin

- Filogic880/BE19000 (4-4-4)
  - eFEM: mt7996_eeprom.bin
  - iFEM: mt7996_eeprom_2i5i6i.bin

- Filogic880/BE14000 (2-3-3)
  - eFEM: mt7996_eeprom_233.bin
  - iFEM: mt7996_eeprom_233_2i5i6i.bin

- Filogic860/BE7200 (4-4), Filogic850/BE7200 (4-4)
  - eFEM: mt7992_eeprom.bin
  - iFEM: mt7992_eeprom_2i5i.bin
  - 2i5e: mt7992_eeprom_2i5e.bin

- Filogic860/BE5000 (2-3)
  - eFEM: mt7992_eeprom_23.bin
  - iFEM: mt7992_eeprom_23_2i5i.bin

- Filogic850/BE3600 (2-3)
  - eFEM: mt7990_eeprom.bin
  - iFEM: mt7990_eeprom_2i5i.bin

## Wi-Fi 8 Latest Release Version
- **Date**: 2026-11-13
- **Modified By**: Evelyn Tsai (evelyn.tsai@mediatek.com)
- **Version**:
  - Driver Version: 5.0.26.11
  - Filogic8800 Firmware Version: 20261104T125931
- **Document Reference**:
  - MAC80211 MT76 Programming Guide v5.0
  - MT76 Test Mode Programming Guide v3.0
- **Summary**:
  - WiFi8: Add Basic  UHR MAC/Phy Cap
    - UHR-MCS 17/19/20/23
    - 3888-bit LPDC (2*LDPC)
    - Unequal Modulation (UEQM)
    - 11bi - SAE w/ EPPKE
    - Enhanced Long Range (ELR)
    - Non-Primary Channel Access (NPCA) - available after 12.B
  - WiFi 7 feature set from the previous release

- **Default Wi-Fi Configuration**

  |                  | 2g               | 5g               | 6g               | MLD                  |
  |------------------|------------------|------------------|------------------|----------------------|
  | **Channel**      | Ch1              | Ch36             | Ch37             | Ch1/36/37           |
  | **Phymode**      | UHR MLO 1-link   | UHR MLO 1-link   | UHR MLO 1-link   | UHR MLO 3-link      |
  | **Bandwidth**    | BW40             | BW160            | BW320            | BW40/160/320        |
  | **Security**     | OPEN             | OPEN             | SAE              | RSNE: PSK2 (AKM2)   |
  |                  |                  |                  |                  | RSNO: SAE (AKM8)    |
  |                  |                  |                  |                  | RSNO2: SAE-EXT (AKM24) |
  | **11vMBSS**      | Disable          | Disable          | Enable           |                      |

#### Filogic 8880 WiFi8 Alpha Release (2026-11-13)
```
# Get OpenWrt 25.12 source code from Git Server
git clone --branch openwrt-25.12 https://git.openwrt.org/openwrt/openwrt.git openwrt

# Get mtk-openwrt-feeds source code
git clone --branch main https://github.com/mediatek/mtk-openwrt-feeds.git

# Choose one SKU to build (1st Build)
cd openwrt

# Select one SKU to build
## All-In-One MTK Reference Board (RFB) and BananaPi BPI-R4
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh filogic-mac80211-mt798x_rfb-wifi8_nic

# Further Build (After 1st full build)
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh build
or
make V=s

# Clean OpenWrt source tree
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh clean
```

## Wi-Fi 7 Latest Release Version

- **Date**: 2026-03-13
- **Modified By**: MeiChia Chiu (meichia.chiu@mediatek.com)
- **Version**:
  - Driver Version: 4.4.26.03
  - Filogic680 Firmware Version: 20260311120504
  - Filogic660 Firmware Version: 20260311085738
  - Filogic650 Firmware Version: 20260311130137
- **Document Reference**:
  - MAC80211 MT76 Programming Guide v4.14
    - The OpenWRT UCI introduce the formal MLO config
    - Lightweight log collection
    - Dynamic single sku power file control
  - MT76 Test Mode Programming Guide v2.7
- **Summary**:
  - Platform
    - Supoprt MT7987/MT7988
    - Support USB/PCIe/Flowblock HWNAT/Thermal
    - Support watchdog, pwm, eip197 look-aside mode, TRNG
    - Support BananaPi BPI-R4, BE14000
    - Security Boot
    - Dual FIP for eMMC only
  - WiFi
    - Real Single Wiphy - foundational requirement for Multi-Link
    - Preamble puncturing (WiFi6E with FCC regularity restriction)
    - WiFi6E Mgmt Power Enhancement
    - Multi-Link Operation (Advertisement/Discovery/Setup)
    - Multi-Link + Phy Features
    - Multi-Link + Security
      - With RSNO (Default on)
      - Without RSNO (transition mode can be used here)
        - with 6GHz (WPA3 only)
        - without 6GHz (WPA3/WPA2)
    - Multi-Link + 4-address WDS
    - Multi-Link + Hardware Peak
    - Multi-Link channel access
      - AP Support: MLSR/EMLSR and MLMR/STR
      - STA Support: MLMR/STR
    - Multi-Link + Multiple Legay BSSID
    - Multi-Link Reconfiguration (Add/Remove Link), w/o WiFi7R2 reconf to setup link
    - Multi-Link Statistics (Per-MLD, Per-Link)
    - Multi-Link Channel switching (including ACS/DFS)
    - Link management (Adv-T2LM & Neg-T2LM)
    - BSS parameter critical update
    - Multi-Link + 11v MBSS
    - Multi-Link + WPS
    - EPCS Priority Access
    - Multi-Link + 11FT (AKM9/25), AP mode only
    - WiFi7 R2 (Implementation ready)
    - **Not Support**:
      - QoS Management R3
  - **Default Wi-Fi Configuration**:

      |                  | 2g      | 5g      | 6g      | MLD                        |
      |------------------|---------|---------|---------|----------------------------|
      | **Channel**      | Ch1     | Ch36    | Ch37    | Ch1/36/37                  |
      | **Bandwidth**    | BW40    | BW160   | BW320   | BW40/160/320               |
      | **Security**     | OPEN    | OPEN    | SAE     | RSNE: PSK2 (AKM2)          |
      |                  |         |         |         | RSNO: SAE (AKM8)           |
      |                  |         |         |         | RSNO2: SAE-EXT (AKM24)     |
      | **11vMBSS**      | Disable | Disable | Enable  |                            |

  - **Notice**:
    - The default power control from user space is disabled to follow the maximum power from eFuse. If you would like to enable power-relevant features (e.g., SingleSKU/iw set Tx Power)
    - Make sure to set 'sku_idx' to zero for a single SKU table or to any positive number for the index of SKU tables you want in the hostapd configuration to enable it.
    - You can double-check whether the value under '/sys/kernel/debug/ieee80211/phy0/mt76/sku_disable' is 0.
#### Filogic 880/850 WiFi7 4.3 Formal Release (2026-03-13)
```
# Get OpenWrt 25.12 source code from Git Server
git clone --branch openwrt-25.12 https://git.openwrt.org/openwrt/openwrt.git openwrt
cd openwrt; git checkout 34ae6ba2d861e710e3c130c95cdb7eb4a1286121; cd -;

# Get mtk-openwrt-feeds source code
git clone --branch main https://github.com/mediatek/mtk-openwrt-feeds.git
cd mtk-openwrt-feeds; git checkout 9b5754e3a361635595ea31a14176b14169e69d18; cd -;

# Choose one SKU to build (1st Build)
cd openwrt

# Change Feeds Revision
# vim ../mtk-openwrt-feeds/autobuild/unified/feed_revision
packages ab3b874a7df14e99179d5f17b77284ad66fa18b7
luci 11264f9297b0ad272f0bd2f288b088f71bf63918
routing 5b23ea12d417e5dba99788c5b34abdae81cccf33

# Select one SKU to build
## All-In-One MTK Reference Board (RFB) and BananaPi BPI-R4
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh filogic-mac80211-mt798x_rfb-wifi7_nic log_file=make

# Further Build (After 1st full build)
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh build
or
make V=s

# Clean OpenWrt source tree
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh clean
```
##### WiFi Package Version

| **Platform**             | **OpenWrt-25.12**             | **github.com/mediatek/mtk-openwrt-feeds**         |
|--------------------------|-------------------------------|-----------------------------------------------------------------------------------|
| Kernel                   | 6.12.74                       | ./feeds/mtk_openwrt_feed/25.12/patches-base <br />  ./feeds/mtk_openwrt_feed/25.12/files  <br /> ./feeds/mtk_openwrt_feed/25.12/patches-feeds              |
| **WiFi Package**         | **OpenWrt-25.12**             | **MTK Internal Patches**                                                          |
| Hostapd                  | PKG_SOURCE_DATE:=2026-02-19   | **Makefile**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/patches-base/0003-hostapd-package-makefile-ucode-files.patch <br /> **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/services/hostapd/patches <br />  **Files**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/nerwork/services/hostapd/files|
| libnl-tiny               | PKG_SOURCE_DATE:=2025-12-02   | N/A                                                                               |
| iw                       | PKG_VERSION:=6.17             | **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/utils/iw/patches |
| iwinfo                   | PKG_SOURCE_DATE:=2026-01-14   | N/A                                                                               |
| wireless-regdb           | PKG_VERSION:=2026-02-04       | N/A                                                                               |
| ucode                    | PKG_VERSION:=2026-01-16       |                                                                                   |
| wifi-scripts             | PKG_VERSION:=1.0              | **Files**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/config/wifi-scripts/files <br /> **Files**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/config/wifi-scripts/files-ucodes|
| netifd                   | PKG_VERSION:=2026-02-26       | N/A                                                                               |
| MAC80211                 | PKG_VERSION:=wireless-next-2026-02-04 (backport-6.18.7)| **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/kernel/mac80211/patches |
| MT76                     | PKG_SOURCE_DATE:=2026-03-05   | **Makefile**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/patches-base/0001-mt76-package-makefile.patch <br /> **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/kernel/mt76/patches <br /> **Firmware**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/kernel/mt76/src/firmware/mt7996 |

#### Filogic 880/850 WiFi7 4.3 Alpha Release (2025-12-31)

```
# Get OpenWrt 25.12 source code from Git Server
git clone --branch openwrt-25.12 https://git.openwrt.org/openwrt/openwrt.git openwrt
cd openwrt; git checkout 2acfd9f8ab12e4f353a0aa644d9adf89588b1f0f; cd -;

# Get mtk-openwrt-feeds source code
git clone --branch main https://github.com/mediatek/mtk-openwrt-feeds.git
cd mtk-openwrt-feeds; git checkout ed73621011e496593b388b267e4da36f059d5f38; cd -;

# Choose one SKU to build (1st Build)
cd openwrt

# Change Feeds Revision
# vim ../mtk-openwrt-feeds/autobuild/unified/feed_revision
packages d3758327421160ede1b03a62c50abcdcd0fbf94a
luci 78a62f261bb956b3246eb8b7a1c85b794d9229a2
routing b43e4ac560ccbafba21dc3ab0dbe57afc07e7b88
video 094bf58da6682f895255a35a84349a79dab4bf95

# Select one SKU to build
## All-In-One MTK Reference Board (RFB) and BananaPi BPI-R4
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh filogic-mac80211-mt798x_rfb-wifi7_nic log_file=make

# Further Build (After 1st full build)
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh build
or
make V=s

# Clean OpenWrt source tree
bash ../mtk-openwrt-feeds/autobuild/unified/autobuild.sh clean
```

##### WiFi Package Version

| **Platform**             | **OpenWrt-25.12**             | **github.com/mediatek/mtk-openwrt-feeds**         |
|--------------------------|-------------------------------|-----------------------------------------------------------------------------------|
| Kernel                   | 6.12.62                       | ./feeds/mtk_openwrt_feed/25.12/patches-base <br />  ./feeds/mtk_openwrt_feed/25.12/files  <br /> ./feeds/mtk_openwrt_feed/25.12/patches-feeds              |
| **WiFi Package**         | **OpenWrt-25.12**             | **MTK Internal Patches**                                                          |
| Hostapd                  | PKG_SOURCE_DATE:=2025-09-29   | **Makefile**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/patches-base/0003-hostapd-package-makefile-ucode-files.patch <br /> **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/services/hostapd/patches <br />  **Files**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/nerwork/services/hostapd/files|
| libnl-tiny               | PKG_SOURCE_DATE:=2025-12-02   | N/A                                                                               |
| iw                       | PKG_VERSION:=6.17             | **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/utils/iw/patches |
| iwinfo                   | PKG_SOURCE_DATE:=2025-11-07   | N/A                                                                               |
| wireless-regdb           | PKG_VERSION:=2025-10-07       | N/A                                                                               |
| ucode                    | PKG_VERSION:=2025-12-01       |                                                                                   |
| wifi-scripts             | PKG_VERSION:=1.0              | **Files**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/config/wifi-scripts/files <br /> **Files**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/network/config/wifi-scripts/files-ucodes|
| netifd                   | PKG_VERSION:=2025-10-20       | N/A                                                                               |
| MAC80211                 | PKG_VERSION:=wireless-next-2025-11-27 (backport-6.18) | **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/kernel/mac80211/patches |
| MT76                     | PKG_SOURCE_DATE:=2025-12-15   | **Makefile**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/patches-base/0001-mt76-package-makefile.patch <br /> **Patches**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/kernel/mt76/patches <br /> **Firmware**: ./feeds/mtk_openwrt_feed/autobuild/unified/filogic/mac80211/25.12/files/package/kernel/mt76/src/firmware/mt7996 |

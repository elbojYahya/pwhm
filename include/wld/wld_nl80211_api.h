/****************************************************************************
**
** SPDX-License-Identifier: BSD-2-Clause-Patent
**
** SPDX-FileCopyrightText: Copyright (c) 2022 SoftAtHome
**
** Redistribution and use in source and binary forms, with or
** without modification, are permitted provided that the following
** conditions are met:
**
** 1. Redistributions of source code must retain the above copyright
** notice, this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above
** copyright notice, this list of conditions and the following
** disclaimer in the documentation and/or other materials provided
** with the distribution.
**
** Subject to the terms and conditions of this license, each
** copyright holder and contributor hereby grants to those receiving
** rights under this license a perpetual, worldwide, non-exclusive,
** no-charge, royalty-free, irrevocable (except for failure to
** satisfy the conditions of this license) patent license to make,
** have made, use, offer to sell, sell, import, and otherwise
** transfer this software, where such license applies only to those
** patent claims, already acquired or hereafter acquired, licensable
** by such copyright holder or contributor that are necessarily
** infringed by:
**
** (a) their Contribution(s) (the licensed copyrights of copyright
** holders and non-copyrightable additions of contributors, in
** source or binary form) alone; or
**
** (b) combination of their Contribution(s) with the work of
** authorship to which such Contribution(s) was added by such
** copyright holder or contributor, if, at the time the Contribution
** is added, such addition causes such combination to be necessarily
** infringed. The patent license shall not apply to any other
** combinations which include the Contribution.
**
** Except as expressly stated above, no rights or licenses from any
** copyright holder or contributor is granted under this license,
** whether expressly, by implication, estoppel or otherwise.
**
** DISCLAIMER
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
** CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
** INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
** MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
** CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
** USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
** AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
** ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
**
****************************************************************************/
/*
 * This file includes nl80211 api (requests) definitions
 */

#ifndef INCLUDE_WLD_WLD_NL80211_API_H_
#define INCLUDE_WLD_WLD_NL80211_API_H_

#include "wld_nl80211_core.h"
#include "wld_nl80211_attr.h"
#include "wld_nl80211_types.h"

#define MLO_LINK_ID_UNKNOWN -1
#define NL80211_DFLT_IFNAME_PFX "wlan"
#define MAX_SUPP_RATES_LEN 12

typedef struct {
    swl_80211_htCapIE_t htCap;
    swl_80211_vhtCapIE_t vhtCap;
    swl_80211_heCapIE_t heCap;
    swl_80211_ehtCapIE_t ehtCap;

    uint8_t supported_rates[MAX_SUPP_RATES_LEN];
    uint8_t supported_rates_len;
} raw_dev_ies_t;
/*
 * @brief return registered handlers table, defining wld implementation for nl80211
 *
 * @return pointer to function table
 *         or NULL if nl80211 implementation was not registered
 */
const T_CWLD_FUNC_TABLE* wld_nl80211_getVendorTable();

#define CALL_NL80211_FTA(fun, arg, ...) \
    { \
        const T_CWLD_FUNC_TABLE* ftaTab = wld_nl80211_getVendorTable(); \
        if((ftaTab != NULL) && (ftaTab->fun != NULL)) { \
            ftaTab->fun(arg, ## __VA_ARGS__); \
        } \
    }

#define CALL_NL80211_FTA_RET(ret, fun, arg, ...) \
    { \
        ret = SWL_RC_NOT_IMPLEMENTED; \
        const T_CWLD_FUNC_TABLE* ftaTab = wld_nl80211_getVendorTable(); \
        if((ftaTab != NULL) && (ftaTab->fun != NULL)) { \
            ret = ftaTab->fun(arg, ## __VA_ARGS__); \
        } \
    }

/*
 * @brief convert time unit into mseconds: 100 TU (102.4ms).
 * Ref: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/nl80211.h#n648
 */
static inline uint64_t wld_nl80211_tu2ms(uint64_t tu) {
    return ((tu * 1024) / 1000);
}

/*
 * @brief convert time unit into mseconds: 100 TU (102.4ms).
 * Ref: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/nl80211.h#n648
 */
static inline uint64_t wld_nl80211_ms2tu(uint64_t ms) {
    return ((ms * 1000) / 1024);
}

/*
 * @brief return registered FSM manager, defining action sequences and state transitions
 * of wld implementation for nl80211.
 * The FSM manager defines the way the scheduled action bitmaps are applied,
 * and also the dependencies between the scheduled actions for Radio, AccessPoint or Endpoint.
 *
 * Technically, every vendor module may (and shall):
 * - define its own STATIC fsm manager, with defining its own action bitmaps
 * to be scheduled when the function table handlers are called.
 * The FSM manager relies on generic wld FSM implementation
 * - share the same FSM manager as nl80211 default implementation
 * when calling wld_fsm_init, while registering the new vendor to wld.
 *
 * @return pointer to FSM manager
 *         or NULL if nl80211 implementation was not registered
 */
const wld_fsmMngr_t* wld_nl80211_getFsmMngr();

/*
 * @brief create wld implementation for nl80211, with provided function table
 *
 * @return pointer to nl80211 vendor context
 */
vendor_t* wld_nl80211_registerVendor(T_CWLD_FUNC_TABLE* fta);

/*
 * @brief get all available nl80211 interfaces in row,
 * sorted per wiphy, and by increasing net dev index
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param pWlIfaces (output) pointer to wl ifaces array, to be freed by the caller
 * @param maxWlIfaces max number of interfaces to retrieve
 * @param pNrWlIfaces (output) pointer to resulting count of wl interfaces
 *
 * @return SWL_RC_OK in case of success
 *         SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getInterfacesList(wld_nl80211_state_t* state, wld_nl80211_ifaceInfo_t** pWlIfaces, uint32_t maxWlIfaces, uint32_t* pNrWlIfaces);

/*
 * @brief get all available nl80211 interfaces sorted per wiphy, and by increasing net dev index, in 2D array per wiphy
 * (Synchronous api)
 *
 * @param nrWiphyMax max wiphy value to fetch (i.e max radios)
 * @param nrWifaceMax max number of interfaces per wiphy (i.e max AP/EP per radio)
 * @param wlIfaces (output) 2D array of interfaces, sorted per wiphy/ifIndex
 *
 * @return SWL_RC_OK in case of success
 *         SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getInterfaces(const uint32_t nrWiphyMax, const uint32_t nrWifaceMax,
                                    wld_nl80211_ifaceInfo_t wlIfaces[nrWiphyMax][nrWifaceMax]);

/*
 * @brief get current nl80211 interface info
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex interface net dev index
 * @param ifName optional interface name, used when ifIndex is null or invalid
 * @param pIfaceInfo (output) interface info
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getInterfaceInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_ifaceInfo_t* pIfaceInfo);

/*
 * @brief create a new virtual interface (AP or EP) on top of wiphy (radio)
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex physical interface net dev index (i.e wiphy / radio iface)
 * @param ifName new virtual interface name
 * @param pMac pointer to bin mac of the new interface
 *        If null pointer, or null address or broadcast address, then mac is assigned by the driver.
 * @param isAp flag to set AccessPoint type for the new interface
 * @param isSta flag to set Station type for the new interface
 *        One at least of isAp or isSta must be true.
 * @param pIfaceInfo (output)(optional) resulting info of the newly created interface.
 *        It will indicate the assigned ifIndex for the new interface, and the used mac address
 *        (shall be the one provided as argument, if mac can be set on interface creation)
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_newInterface(wld_nl80211_state_t* state, uint32_t IfIndex, const char* ifName,
                                   const swl_macBin_t* pMac, bool isAp, bool isSta,
                                   wld_nl80211_ifaceInfo_t* pIfaceInfo);

/*
 * @brief create a new virtual interface on top of wiphy (radio)
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex physical interface net dev index (i.e wiphy / radio iface)
 * @param ifName new virtual interface name
 * @param pIfaceConf new virtual interface conf (type: (AP, EP or monitor), mac address)
 * @param pIfaceInfo (output)(optional) resulting info of the newly created interface.
 *        It will indicate the assigned ifIndex for the new interface, and the used mac address
 *        (shall be the one provided as argument, if mac can be set on interface creation)
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_newInterfaceExt(wld_nl80211_state_t* state, uint32_t ifIndex, const char* ifName,
                                      wld_nl80211_newIfaceConf_t* pIfaceConf,
                                      wld_nl80211_ifaceInfo_t* pIfaceInfo);

/*
 * @brief create a new virtual interface on top of wiphy (radio)
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param wiphyId physical device index
 * @param ifName new virtual interface name
 * @param pIfaceConf new virtual interface conf (type: (AP, EP or monitor), mac address)
 * @param pIfaceInfo (output)(optional) resulting info of the newly created interface.
 *        It will indicate the assigned ifIndex for the new interface, and the used mac address
 *        (shall be the one provided as argument, if mac can be set on interface creation)
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_newWiphyInterface(wld_nl80211_state_t* state, uint32_t wiphyId, const char* ifName,
                                        wld_nl80211_newIfaceConf_t* pIfaceConf,
                                        wld_nl80211_ifaceInfo_t* pIfaceInfo);

/*
 * @brief add default wl AP ifaces for all bands of available wiphy devices
 * (input: all available nl80211 interfaces sorted per wiphy, and by increasing net dev index, in 2D array per wiphy)
 * (Synchronous api)
 *
 * @param custIfNamePfx custom wl interface name prefix (if empty, default prefix "wlan" is used)
 * @param maxWiphys max wiphy value to fetch (i.e max wiphy devices)
 * @param maxWlIfaces max number of interfaces per wiphy (i.e max AP/EP per radio)
 * @param wlIfacesInfo (In/out) 2D array of interfaces, used as input for existing wiphy/ifIndex
 *                              and filled with potential added wl AP ifaces info struct
 *
 * @return SWL_RC_OK in case of success
 *         SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_addDefaultWiphyInterfacesExt(const char* custIfNamePfx,
                                                   const uint32_t maxWiphys, const uint32_t maxWlIfaces,
                                                   wld_nl80211_ifaceInfo_t wlIfacesInfo[maxWiphys][maxWlIfaces]);

/*
 * @brief delete a virtual interface (AP or EP)
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex interface net dev index
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_delInterface(wld_nl80211_state_t* state, uint32_t ifIndex);

/*
 * @brief register to frames
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex interface net dev index
 * @param type packet type to be received
 * @param pattern first bytes of the packet (optional)
 * @param patternLen pattern's length (optional)
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_registerFrame(wld_nl80211_state_t* state, uint32_t ifIndex, uint16_t type, const char* pattern, size_t patternLen);

/*
 * @brief configure interface as AccessPoint or Station
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex interface net dev index
 * @param isAP flag for AP mode
 * @param isSta flag for STA mode
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_setInterfaceType(wld_nl80211_state_t* state, uint32_t ifIndex, bool isAp, bool isSta);

/*
 * @brief configure interface to user 4MAC mode
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex interface net dev index
 * @param use4Mac flag to use 4MAC mode
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_setInterfaceUse4Mac(wld_nl80211_state_t* state, uint32_t ifIndex, bool use4Mac);


/*
 * @brief count current number of registered nl80211 wiphy devices
 * in sysfs /sys/class/ieee80211/
 *
 *
 * @return >= 0 in case of success
 *         -1 otherwise
 */
int32_t wld_nl80211_countWiphyFromFS();

/*
 * @brief get wiphy (radio) info: radio caps, supported bands/chans, dfs status, operStds, ...)
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param pWiphyInfo (output) wiphy info
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getWiphyInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_wiphyInfo_t* pWiphyInfo);

/*
 * @brief get all wiphy interfaces
 * (Synchronous api)
 *
 * @param nrWiphyMax max wiphy value to fetch (i.e max radios)
 * @param pWiphyIfs (output) array of wiphy interfaces
 * @param pNrWiphy (output) number of detected wiphy interfaces
 *
 * @return SWL_RC_OK in case of success
 *         SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getAllWiphyInfo(wld_nl80211_state_t* state, const uint32_t nrWiphyMax, wld_nl80211_wiphyInfo_t pWiphyIfs[nrWiphyMax], uint32_t* pNrWiphy);

/*
 * @brief get vendor wiphy (radio) info: radio caps, supported bands/chans, dfs status, operStds, ...)
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param vendorHandler callback invoked when a reply is available
 * @param vendorData private data to pass in to the handler
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getVendorWiphyInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_handler_f vendorHandler, void* vendorData);

/*
 * @brief get station info: rx/tx bytes, rx/tx packets, rssi, ...
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex parent interface index
 * @param pMac pointer to station mac address
 * @param pSationInfo (output) station info
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getStationInfo(wld_nl80211_state_t* state, uint32_t ifIndex, const swl_macBin_t* pMac, wld_nl80211_stationInfo_t* pSationInfo);

/*
 * @brief get all paired stations info on a specific interface
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex parent interface index
 * @param ppStationInfo (output) array of station info, dynamically allocated
 * (need to be freed by user)
 * @param pnrStation (output) number of available stations
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 * @
 */
swl_rc_ne wld_nl80211_getAllStationsInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_stationInfo_t** ppStationInfo, uint32_t* pnrStation);

/*
 * @brief get survey info of all radio channels
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param ppChanSurveyInfo (output) array of channel survey info, dynamically allocated
 * (need to be freed by user)
 * @param pnChanSurveyInfo (output) number of available survey info
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getSurveyInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_channelSurveyInfo_t** ppChanSurveyInfo, uint32_t* pnChanSurveyInfo);

/*
 * @brief get survey info of all radio channels
 * (Synchronous api)
 * This is extended version with survey dump params to filter results
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param pConfig survey dump config params, ignored is null
 * @param ppChanSurveyInfo (output) array of channel survey info, dynamically allocated
 * (need to be freed by user)
 * @param pnChanSurveyInfo (output) number of available survey info
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getSurveyInfoExt(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_channelSurveyParam_t* pConfig,
                                       wld_nl80211_channelSurveyInfo_t** ppChanSurveyInfo, uint32_t* pnChanSurveyInfo);

/*
 * @brief configure tx/rx antennas
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param txMapAnt Bitmap of allowed antennas for transmitting
 * @param rxMapAnt Bitmap of allowed antennas for receiving
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_setWiphyAntennas(wld_nl80211_state_t* state, uint32_t ifIndex, uint32_t txMapAnt, uint32_t rxMapAnt);

/*
 * @brief configure automatic transmit power level
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_setTxPowerAuto(wld_nl80211_state_t* state, uint32_t ifIndex);

/*
 * @brief configure fixed transmit power level
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param mbm transmit power level in dbm
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_setTxPowerFixed(wld_nl80211_state_t* state, uint32_t ifIndex, int32_t dbm);

/*
 * @brief configure limited transmit power level
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param mbm transmit power level in dbm
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_setTxPowerLimited(wld_nl80211_state_t* state, uint32_t ifIndex, int32_t dbm);

/*
 * @brief get transmit power level
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param dbm current tx power in dbm
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getTxPower(wld_nl80211_state_t* state, uint32_t ifIndex, int32_t* dbm);

/*
 * @brief get maximum transmit power level
 * (Synchronous api)
 *
 * @param state nl80211 socket manager context
 * @param ifIndex wiphy main iface index
 * @param freq (in MHz) for which the max transmit power is being retrieved.
 * @param dbm max tx power in dbm
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getMaxTxPowerdBm(wld_nl80211_state_t* state, uint32_t ifIndex, uint32_t freq, int32_t* dbm);

/*
 * @brief initiate a neighbor scan
 * It is possible to indicate scan options:
 * - ssids to fetch
 * - frequencies to use
 * - extra InfoElements to add to scan probe request frames
 *
 * @param state nl80211 socket manager
 * @param ifIndex network interface index indicating relative radio device
 * @param params scan options (ssids, frequencies, IEs)
 *
 * @return SWL_RC_OK in case of success (scan trigger acknowledged)
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_startScan(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_scanParams_t* params);

/*
 * @brief abort a running neighbor scan
 *
 * @param state nl80211 socket manager
 * @param ifIndex network interface index indicating relative radio device
 *
 * @return SWL_RC_OK in case of success (scan abort acknowledged)
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_abortScan(wld_nl80211_state_t* state, uint32_t ifIndex);

/*
 * @brief handler prototype to get asynchronous scan results
 *
 * @param priv Private user data
 * @param results pointer to scan results structure
 */
typedef void (* scanResultsCb_f) (void* priv, swl_rc_ne rc, wld_scanResults_t* results);

/*
 * @brief subscribe to get asynchronous scan results
 *
 * @param state nl80211 socket manager
 * @param ifIndex network interface index indicating relative radio device
 * @param priv user data that will returned in the result handler
 * @param fScanResultsCb handler that will be called when results are ready
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getScanResults(wld_nl80211_state_t* state, uint32_t ifIndex, void* priv, scanResultsCb_f fScanResultsCb);

/*
 * @brief subscribe to get asynchronous scan results, filtered by freq band
 *
 * @param state nl80211 socket manager
 * @param ifIndex network interface index indicating relative radio device
 * @param priv user data that will returned in the result handler
 * @param fScanResultsCb handler that will be called when results are ready
 * @param band frequency band to be matched (AUTO means any freqBand)
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getScanResultsPerFreqBand(wld_nl80211_state_t* state, uint32_t ifIndex, void* priv, scanResultsCb_f fScanResultsCb,
                                                swl_freqBandExt_e band);

/*
 * @brief set regulatory ISO/IEC 3166-1 alpha2 country code
 * (applicable as global or on phy device)
 *
 * @param state nl80211 socket manager context
 * @param wiphy phy device index (use WLD_NL80211_ID_ANY to set global reg domain)
 * @param alpha2 ISO/IEC 3166-1 alpha2 country code
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_setRegDomain(wld_nl80211_state_t* state, uint32_t wiphy, const char* alpha2);

/*
 * @brief start a background DFS
 *
 * @param state nl80211 socket manager
 * @param ifIndex network interface index indicating relative device
 * @param ifMloLinkId network interface MLO Link ID, otherwise MLO_LINK_ID_UNKNOWN for non-MLD
 * @param bgDfsChanspec background DFS chanspec
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_bgDfsStart(wld_nl80211_state_t* state, uint32_t ifIndex, int8_t ifMloLinkId, swl_chanspec_t bgDfsChanspec);

/*
 * @brief stop a background DFS
 *
 * @param state nl80211 socket manager
 * @param ifIndex network interface index indicating relative device
 * @param ifMloLinkId network interface MLO Link ID, otherwise MLO_LINK_ID_UNKNOWN for non-MLD
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_bgDfsStop(wld_nl80211_state_t* state, uint32_t ifIndex, int8_t ifMloLinkId);

/*
 * @brief common function to send vendor sub command
 *
 * @param state nl80211 socket manager context
 * @param oui vendor driver identifier
 * @param subcmd vendor sub command to be sent
 * @param data optional depending on sent sub command
 * @param dataLen length of sent data
 * @param isSync flag to send sync/async request
 * @param withAck flag to wait for acknowledgment request
 * @param flags optional nl80211 msg flags
 * @param ifIndex interface net dev index (ignored if ifIndex is null)
 * @param wDevId interface wdev index
 * @param handler callback invoked when a reply is available
 * @param priv private data to pass in to the handler
 *
 * @return SWL_RC_OK on success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_sendVendorSubCmd(wld_nl80211_state_t* state, uint32_t oui, int subcmd, void* data, int dataLen,
                                       bool isSync, bool withAck, uint32_t flags, uint32_t ifIndex, uint64_t wDevId,
                                       wld_nl80211_handler_f handler, void* priv);

/*
 * @brief common function to send vendor sub command with attributes inside vendor data
 *
 * @param state nl80211 socket manager context
 * @param oui vendor driver identifier
 * @param subcmd vendor sub command to be sent
 * @param vendorAttr list of attributes to be added inside the vendor data attributes.
 * @param isSync flag to send sync/async request
 * @param withAck flag to wait for acknowledgment request
 * @param flags optional nl80211 msg flags
 * @param ifIndex interface net dev index (ignored if ifIndex is null)
 * @param wDevId interface wdev index
 * @param handler callback invoked when a reply is available
 * @param priv private data to pass in to the handler
 *
 * @return SWL_RC_OK on success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_sendVendorSubCmdAttr(wld_nl80211_state_t* state, uint32_t oui, int subcmd, wld_nl80211_nlAttr_t* vendorAttr,
                                           bool isSync, bool withAck, uint32_t flags, uint32_t ifIndex, uint64_t wDevId,
                                           wld_nl80211_handler_f handler, void* priv);

/*
 * @brief common function to send a management frame cmd
 *
 * @param state nl80211 socket manager context
 * @param fc frame control content
 * @param data data to be sent
 * @param chanspec destination channel to send the frame
 * @param src source MACAddress to be written in header
 * @param dst destination MACAddress to be written in header
 * @param bssid BSSID MACAddress to be written in header
 * @param flags optional nl80211 msg flags
 * @param ifIndex interface net dev index
 * @param ifMloLinkId network interface MLO Link ID, otherwise MLO_LINK_ID_UNKNOWN for non-MLD
 *
 * @return SWL_RC_OK on success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_sendManagementFrameCmd(wld_nl80211_state_t* state, swl_80211_mgmtFrameControl_t* fc, swl_bit8_t* data, size_t dataLen,
                                             swl_chanspec_t* chanspec, swl_macBin_t* src, swl_macBin_t* dst, swl_macBin_t* bssid, uint32_t flags,
                                             uint32_t ifIndex, int8_t ifMloLinkId);


/*
 * @brief common function to verify callback status and return data
 *
 * @param rc result of initial checks of the reply
 * @param nlh netlink header of received msg, to be parsed
 * @param data (output) parameter to forward provided data on request success
 * @param dataLen (output) parameter to forward provided data length on request success
 *
 * @return SWL_RC_OK when message is valid, handled, but waiting for next parts
 *         SWL_RC_CONTINUE when message is valid, but ignored
 *         SWL_RC_DONE when message is processed and all reply parts are received:
 *                     request is terminated and can be cleared
 *         SWL_RC_ERROR in case of error: request will be cancelled
 */
swl_rc_ne wld_nl80211_getVendorDataFromVendorMsg(swl_rc_ne rc, struct nlmsghdr* nlh, void** data, size_t* dataLen);

/*
 * @brief convert nl80211 channel desc into swl format
 *
 * @param pSwlChanSpec pointer to swl chanspec info struct to be filled
 * @param pNlChanSpec source pointer to nl80211 chspec info
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_chanSpecNlToSwl(swl_chanspec_t* pSwlChanSpec, wld_nl80211_chanSpec_t* pNlChanSpec);

/*
 * @brief get channel specification from nl80211 interface info
 *
 * @param pChanSpec pointer to chanspec info struct to be filled
 * @param pIfaceInfo source pointer to interface info
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getChanSpecFromIfaceInfo(swl_chanspec_t* pChanSpec, wld_nl80211_ifaceInfo_t* pIfaceInfo);

/*
 * @brief get current channel info
 *
 * @param state nl80211 socket manager context
 * @param ifIndex interface net dev index
 * @param pChanSpec pointer to chanspec info struct to be filled
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_getChanSpec(wld_nl80211_state_t* state, uint32_t ifIndex, swl_chanspec_t* pChanSpec);

/*
 * @brief seeking by mac for interface MLO link info into included interface MLO links
 *
 * @param pIface pointer to input mld interface info
 * @param pLinkMac link mac address
 *
 * @return pointer to found interface MLO link info, NULL otherwise
 */
const wld_nl80211_ifaceMloLinkInfo_t* wld_nl80211_fetchIfaceMloLinkByMac(wld_nl80211_ifaceInfo_t* pIface, swl_macBin_t* pLinkMac);

/*
 * @brief seeking by linkId for interface MLO link info into included interface MLO links
 *
 * @param pIface pointer to input mld interface info
 * @param linkId MLO link id to look for
 *
 * @return pointer to found interface MLO link info, NULL otherwise
 */
const wld_nl80211_ifaceMloLinkInfo_t* wld_nl80211_fetchIfaceMloLinkById(wld_nl80211_ifaceInfo_t* pIface, int32_t linkId);

/*
 * @brief get by position the interface MLO link info
 *
 * @param pIface pointer to input mld interface info
 * @param linkPos link position (0: primary, 1..X: secondaryX..)
 *
 * @return pointer to found interface MLO link info, NULL otherwise
 */
const wld_nl80211_ifaceMloLinkInfo_t* wld_nl80211_getIfaceMloLinkAtPos(wld_nl80211_ifaceInfo_t* pIface, uint32_t linkPos);

/*
 * @brief find mld interface hosting specific mlo link mac
 *
 * @param state nl80211 socket manager context
 * @param pLinkMac link mac address
 * @param pIface (output) pointer to resulting mld interface info
 * @param pLinkId (output) pointer filled with mld link id
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_findMldIfaceByLinkMac(wld_nl80211_state_t* state, swl_macBin_t* pLinkMac, wld_nl80211_ifaceInfo_t* pIface, int32_t* pLinkId);

/*
 * @brief Manually add a station
 *
 * @param state nl80211 socket manager context
 * @param iface_idx The index of the interface to add the station to
 * @param mac The MAC address of the station
 * @param assoc_req_data The association request data corresponding to the station. Used to set the capabilities and other info.
 * @param assoc_req_len The length of the association request data
 * @param aid The association ID of the station
 * @param flags The bitmask of flags to set for the station (e.g. `NL80211_STA_FLAG_AUTHORIZED`, `NL80211_STA_FLAG_ASSOCIATED`)
 *
 * @note The `NL80211_CMD_NEW_STATION` command will fail with an `EINVAL` error if the `NL80211_STA_FLAG_ASSOCIATED` flag is "set"
 *  and the `NL80211_FEATURE_FULL_AP_CLIENT_STATE` feature is not supported by the driver.
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_nl80211_addStation(wld_nl80211_state_t* state, uint32_t iface_idx, swl_macBin_t* mac, uint8_t* assoc_req_data, size_t assoc_req_len, uint16_t aid, uint16_t flags);

#endif /* INCLUDE_WLD_WLD_NL80211_API_H_ */

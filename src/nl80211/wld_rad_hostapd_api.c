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
#include "wld_util.h"
#include "wld_channel.h"
#include "wld_radio.h"
#include "wld_accesspoint.h"
#include "wld_wpaCtrl_api.h"
#include "wld_rad_hostapd_api.h"
#include "wld_hostapd_ap_api.h"
#include "wld_hostapd_cfgFile.h"
#include "swl/map/swl_mapCharFmt.h"

#define ME "hapdRad"

static wld_wpaCtrlInterface_t* s_getFirstReadyIface(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, NULL, ME, "NULL");
    wld_wpaCtrlMngr_t* pMgr = wld_secDmn_getWpaCtrlMgr(pRad->hostapd);
    return wld_wpaCtrlMngr_getFirstReadyInterface(pMgr);
}

static bool s_sendHostapdCommand(T_Radio* pR, char* cmd, const char* reason) {
    ASSERT_NOT_NULL(pR, false, ME, "NULL");
    wld_wpaCtrlInterface_t* wpaCtrlInterface = s_getFirstReadyIface(pR);
    ASSERT_NOT_NULL(wpaCtrlInterface, false, ME, "%s: hostapd has no wpactrl iface ready", pR->Name);
    SAH_TRACEZ_INFO(ME, "%s: send hostapd cmd %s for %s",
                    wld_wpaCtrlInterface_getName(wpaCtrlInterface), cmd, reason);
    return wld_wpaCtrl_sendCmdCheckResponse(wpaCtrlInterface, cmd, "OK");
}

/** @brief update the the Operating Standard in the hostapd
 *
 * @param pRad radio
 * @return - SECDMN_ACTION_OK_NEED_SIGHUP when Operating Standard is updated in the hostapd config.
 *         - Otherwise SECDMN_ACTION_ERROR.
 */
wld_secDmn_action_rc_ne wld_rad_hostapd_setOperatingStandard(T_Radio* pRadio) {
    ASSERTS_NOT_NULL(pRadio, SECDMN_ACTION_ERROR, ME, "NULL");

    // overwrite the current hostapd config file
    wld_hostapd_cfgFile_createExt(pRadio);

    return SECDMN_ACTION_OK_NEED_SIGHUP;
}

/** @brief calculate the sec_channel_offset parameter of CHAN_SWITCH hostapd command
 * the function code is inspired from hostapd_ctrl_check_freq_params function in hostapd
 *
 * @param bandwidth bandwidht 20/40/80/160MHz
 * @param freq frequency
 * @param center_freq center frequency
 * @param sec_channel_offset a parameter in the CHAN_SWITCH command
 * @return - SWL_RC_OK when the command is sent successfully
 *         - Otherwise SWL_RC_ERROR
 */
static swl_rc_ne s_calculateSecChannelOffset(swl_bandwidth_e bandwidth, uint32_t freq, uint32_t center_freq, int* sec_channel_offset) {
    swl_rc_ne ret = SWL_RC_OK;

    switch(bandwidth) {
    case SWL_BW_AUTO:
    case SWL_BW_20MHZ:
        *sec_channel_offset = 0;
        break;
    case SWL_BW_40MHZ:
        if(freq + 10 == center_freq) {
            *sec_channel_offset = 1;
        } else if(freq - 10 == center_freq) {
            *sec_channel_offset = -1;
        } else {
            ret = SWL_RC_ERROR;
        }
        break;
    case SWL_BW_80MHZ:
        if((freq - 10 == center_freq) || (freq + 30 == center_freq)) {
            *sec_channel_offset = 1;
        } else if((freq + 10 == center_freq) || (freq - 30 == center_freq)) {
            *sec_channel_offset = -1;
        } else {
            ret = SWL_RC_ERROR;
        }
        break;
    case SWL_BW_160MHZ:
        if((freq + 70 == center_freq) || (freq + 30 == center_freq) ||
           (freq - 10 == center_freq) || (freq - 50 == center_freq)) {
            *sec_channel_offset = 1;
        } else if((freq + 50 == center_freq) || (freq + 10 == center_freq) ||
                  (freq - 30 == center_freq) || (freq - 70 == center_freq)) {
            *sec_channel_offset = -1;
        } else {
            ret = SWL_RC_ERROR;
        }
        break;
    case SWL_BW_320MHZ:
        if((freq + 150 == center_freq) || (freq + 110 == center_freq) ||
           (freq - 90 == center_freq) || (freq - 130 == center_freq) ||
           (freq + 70 == center_freq) || (freq + 30 == center_freq) ||
           (freq - 10 == center_freq) || (freq - 50 == center_freq)) {
            *sec_channel_offset = 1;
        } else if((freq + 130 == center_freq) || (freq + 90 == center_freq) ||
                  (freq - 110 == center_freq) || (freq - 150 == center_freq) ||
                  (freq + 50 == center_freq) || (freq + 10 == center_freq) ||
                  (freq - 30 == center_freq) || (freq - 70 == center_freq)) {
            *sec_channel_offset = -1;
        } else {
            ret = SWL_RC_ERROR;
        }
        break;
    default:
        ret = SWL_RC_ERROR;
        break;
    }
    return ret;
}
/**
 * @brief switch the channel
 *
 * @param pRad radio
 *
 * @return - SWL_RC_OK when the command is sent successfully
 *         - Otherwise SWL_RC_ERROR or SWL_RC_INVALID_PARAM
 */
swl_rc_ne wld_rad_hostapd_switchChannel(T_Radio* pR) {
    T_AccessPoint* primaryVap = wld_rad_hostapd_getCfgMainVap(pR);
    ASSERT_NOT_NULL(primaryVap, SWL_RC_INVALID_PARAM, ME, "NULL");

    swl_chanspec_t chanspec = pR->targetChanspec.chanspec;

    uint32_t freq;
    uint32_t center_freq;

    swl_rc_ne res = swl_chanspec_channelToMHz(&chanspec, &freq);
    ASSERT_TRUE(res == SWL_RC_OK, SWL_RC_ERROR, ME, "Invalid band");

    int32_t bw = swl_chanspec_bwToInt(chanspec.bandwidth);

    swl_channel_t center_channel = swl_chanspec_getCentreChannel(&chanspec);
    chanspec.channel = center_channel;

    res = swl_chanspec_channelToMHz(&chanspec, &center_freq);
    ASSERT_TRUE(res == SWL_RC_OK, SWL_RC_ERROR, ME, "Invalid band");

    // Extension Channel
    int sec_channel_offset = 0;
    res = s_calculateSecChannelOffset(chanspec.bandwidth, freq, center_freq, &sec_channel_offset);
    ASSERT_TRUE(res == SWL_RC_OK, SWL_RC_ERROR, ME, "invalid frequencies");

    char cmd[256] = {'\0'};
    swl_str_catFormat(cmd, sizeof(cmd), "CHAN_SWITCH");
    swl_strlst_catFormat(cmd, sizeof(cmd), " ", "5"); // beacons count before switch
    swl_strlst_catFormat(cmd, sizeof(cmd), " ", "%d", freq);

    swl_strlst_catFormat(cmd, sizeof(cmd), " ", "bandwidth=%d", bw);
    swl_strlst_catFormat(cmd, sizeof(cmd), " ", "center_freq1=%d", center_freq);
    if(bw > 20) {
        swl_strlst_catFormat(cmd, sizeof(cmd), " ", "sec_channel_offset=%d", sec_channel_offset);
    }

    // standard_mode
    swl_radioStandard_m operStd = pR->operatingStandards;
    if(SWL_BIT_IS_ONLY_SET(operStd, SWL_RADSTD_AUTO)) {
        operStd = pR->supportedStandards;
    }
    if(SWL_BIT_IS_SET(operStd, SWL_RADSTD_N)) {
        swl_strlst_catFormat(cmd, sizeof(cmd), " ", "ht");
    }
    if(SWL_BIT_IS_SET(operStd, SWL_RADSTD_AC)) {
        swl_strlst_catFormat(cmd, sizeof(cmd), " ", "vht");
    }
    if(SWL_BIT_IS_SET(operStd, SWL_RADSTD_AX)) {
        swl_strlst_catFormat(cmd, sizeof(cmd), " ", "he");
    }
    if(SWL_BIT_IS_SET(operStd, SWL_RADSTD_BE)) {
        swl_strlst_catFormat(cmd, sizeof(cmd), " ", "eht");
    }

    /* TODO: punct_bitmap */
    /* TODO: blocktx */

    //send command
    bool ret = wld_ap_hostapd_sendCommand(primaryVap, cmd, "switch channel");
    ASSERTS_TRUE(ret, SWL_RC_ERROR, ME, "NULL");
    return SWL_RC_OK;
}

/**
 * @brief reload hostapd from memory
 *
 * @param pR radio context
 * @return SWL_RC_OK when the Hostapd  is reloaded. Otherwise error code.
 */
swl_rc_ne wld_rad_hostapd_reload(T_Radio* pR) {
    ASSERT_NOT_NULL(pR, SWL_RC_INVALID_PARAM, ME, "NULL");
    bool ret = s_sendHostapdCommand(pR, "RELOAD", "reload conf");
    ASSERTS_TRUE(ret, SWL_RC_ERROR, ME, "%s: reload fail", pR->Name);
    return SWL_RC_OK;
}

/**
 * @brief reconfigure hostapd from file
 *
 * @param pR radio context
 * @return SWL_RC_OK when the Hostapd config is reloaded. Otherwise error code.
 */
swl_rc_ne wld_rad_hostapd_reconfigure(T_Radio* pR) {
    ASSERT_NOT_NULL(pR, SWL_RC_INVALID_PARAM, ME, "NULL");
    wld_wpaCtrlMngr_t* pMgr = wld_secDmn_getWpaCtrlMgr(pR->hostapd);
    wld_secDmn_t* pSecDmn = wld_wpaCtrlMngr_getSecDmn(pMgr);
    ASSERT_NOT_NULL(pSecDmn, SWL_RC_INVALID_STATE, ME, "NULL");
    wld_wpaCtrlInterface_t* pIface = wld_wpaCtrlMngr_getDefaultInterface(pMgr);
    ASSERT_NOT_NULL(pIface, SWL_RC_INVALID_STATE, ME, "%s: hostapd has no wpactrl iface available", pR->Name);
    swl_rc_ne rc = SWL_RC_INVALID_STATE;

    /*
     * learning support of UPDATE cmd
     */
    swl_trl_e trl = wld_secDmn_getCmdSupp(pSecDmn, "UPDATE");
    if(trl != SWL_TRL_FALSE) {
        rc = wld_wpaCtrl_sendCmdFmtCheckResponse(pIface, "OK", "UPDATE ");
        //the call may timeout while being applied, but that only happens on success
        if(rc == SWL_RC_NOT_AVAILABLE) {
            rc = SWL_RC_OK;
        }
        if((trl == SWL_TRL_UNKNOWN) && (rc >= SWL_RC_ERROR)) {
            trl = (rc == SWL_RC_OK);
            wld_secDmn_setCmdSupp(pSecDmn, "UPDATE", trl);
        }
        if(trl == SWL_TRL_TRUE) {
            return rc;
        }
    }

    /*
     * learning support of RELOAD_CONFIG cmd
     */
    rc = SWL_RC_INVALID_PARAM;
    trl = wld_secDmn_getCmdSupp(pSecDmn, "RELOAD_CONFIG");
    if(trl != SWL_TRL_FALSE) {
        rc = wld_wpaCtrl_sendCmdFmtCheckResponse(pIface, "OK", "RELOAD_CONFIG");
        if(rc == SWL_RC_NOT_AVAILABLE) {
            rc = SWL_RC_OK;
        }
        if((trl == SWL_TRL_UNKNOWN) && (rc >= SWL_RC_ERROR)) {
            trl = (rc == SWL_RC_OK);
            wld_secDmn_setCmdSupp(pSecDmn, "RELOAD_CONFIG", trl);
        }
        if(trl == SWL_TRL_TRUE) {
            return rc;
        }
    }

    /*
     * Before hostapd 2.11 (rev 0102c5c6067f15af4fa2ee0ca5bc0ed70b62ffc1):
     * hostapd was using prefix matching for ENABLE/RELOAD/DISABLE
     * which is confusing about RELOAD_CONFIG support
     * temporary workaround: using cmd line to detect support
     */
    char buffer[128] = {0};
    SWL_EXEC_BUF(buffer, sizeof(buffer), "hostapd_cli", "-i %s reload_config", wld_wpaCtrlInterface_getName(pIface));
    if(!swl_str_isEmpty(buffer)) {
        char* p = strrchr(buffer, '\n');
        W_SWL_SETPTR(p, 0);
        rc = swl_str_matches(buffer, "OK") ? SWL_RC_OK : SWL_RC_ERROR;
        wld_secDmn_setCmdSupp(pSecDmn, "RELOAD_CONFIG", (rc == SWL_RC_OK));
    }

    return rc;
}

/**
 * @brief set main interface channel parameters
 *
 * @param pR radio context
 *
 * @return SECDMN_ACTION_OK_NEED_TOGGLE when the Hostapd config is set. Otherwise SECDMN_ACTION_ERROR.
 */
wld_secDmn_action_rc_ne wld_rad_hostapd_setChannel(T_Radio* pR) {
    T_AccessPoint* primaryVap = wld_rad_hostapd_getCfgMainVap(pR);
    ASSERT_NOT_NULL(primaryVap, SECDMN_ACTION_ERROR, ME, "NULL");
    swl_mapChar_t radParams;
    swl_mapChar_init(&radParams);
    wld_hostapd_cfgFile_setRadioConfig(pR, &radParams);
    const char* chanParams[] = {
        "channel", "op_class", "ht_capab",
        "vht_capab", "vht_oper_centr_freq_seg0_idx", "vht_oper_chwidth",
        "he_oper_centr_freq_seg0_idx", "he_oper_chwidth",
        "eht_oper_centr_freq_seg0_idx", "eht_oper_chwidth",
    };
    for(uint32_t i = 0; i < SWL_ARRAY_SIZE(chanParams); i++) {
        wld_ap_hostapd_setParamValue(primaryVap, chanParams[i], swl_mapChar_get(&radParams, (char*) chanParams[i]), "");
    }
    swl_mapChar_cleanup(&radParams);
    return SECDMN_ACTION_OK_NEED_TOGGLE;
}

wld_secDmn_action_rc_ne wld_rad_hostapd_setMiscParams(T_Radio* pRad) {
    T_AccessPoint* primaryVap = wld_rad_hostapd_getCfgMainVap(pRad);
    ASSERT_NOT_NULL(primaryVap, SECDMN_ACTION_ERROR, ME, "NULL");

    swl_mapChar_t radParams;
    swl_mapChar_init(&radParams);
    wld_hostapd_cfgFile_setRadioConfig(pRad, &radParams);
    const char* miscRadParams[] = {
        "rts_threshold", "preamble",
        "supported_rates", "basic_rates",
        "he_bss_color", "he_spr_srg_partial_bssid", "he_bss_color_partial",
        "he_spr_sr_control", "he_spr_non_srg_obss_pd_max_offset",
        "he_spr_srg_obss_pd_min_offset", "he_spr_srg_obss_pd_max_offset",
        "he_spr_srg_bss_colors", "mbssid", "hw_mode", "ieee80211n",
        "ieee80211ac", "ieee80211ax", "ieee80211be",
    };
    for(uint32_t i = 0; i < SWL_ARRAY_SIZE(miscRadParams); i++) {
        wld_ap_hostapd_setParamValue(primaryVap, miscRadParams[i], swl_mapChar_get(&radParams, (char*) miscRadParams[i]), "");
    }
    swl_mapChar_cleanup(&radParams);

    swl_mapChar_t vapParams;
    swl_mapChar_init(&vapParams);
    wld_hostapd_cfgFile_setVapConfig(primaryVap, &vapParams, (swl_mapChar_t*) NULL);
    const char* miscVapParams[] = {
        "beacon_int", "dtim_period",
    };
    amxc_llist_for_each(it, &pRad->llAP) {
        T_AccessPoint* pAP = amxc_llist_it_get_data(it, T_AccessPoint, it);
        for(uint32_t i = 0; i < SWL_ARRAY_SIZE(miscVapParams); i++) {
            wld_ap_hostapd_setParamValue(pAP, miscVapParams[i], swl_mapChar_get(&vapParams, (char*) miscVapParams[i]), "");
        }
    }
    swl_mapChar_cleanup(&vapParams);
    return SECDMN_ACTION_OK_NEED_TOGGLE;
}

/**
 * @brief enable main hostapd interface
 *
 * @param pR radio context
 *
 * @return SWL_RC_OK when the enabling cmd is sent. Otherwise error code.
 */
swl_rc_ne wld_rad_hostapd_enable(T_Radio* pR) {
    ASSERT_NOT_NULL(pR, SWL_RC_INVALID_PARAM, ME, "NULL");
    wld_wpaCtrlMngr_t* pMgr = wld_secDmn_getWpaCtrlMgr(pR->hostapd);
    wld_wpaCtrlInterface_t* pIface = wld_wpaCtrlMngr_getFirstReadyInterface(pMgr);
    ASSERT_NOT_NULL(pIface, SWL_RC_ERROR, ME, "%s: hostapd has no wpactrl iface ready", pR->Name);
    bool ret = wld_wpaCtrl_sendCmdCheckResponseExt(pIface, "ENABLE", "OK", 5000);
    ASSERTS_TRUE(ret, SWL_RC_ERROR, ME, "%s: enabling failed", wld_wpaCtrlInterface_getName(pIface));
    return SWL_RC_OK;
}

swl_rc_ne wld_rad_hostapd_updateAllVapsConfigId(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, SWL_RC_ERROR, ME, "pRad is NULL");
    ASSERTS_NOT_NULL(pRad->hostapd, SWL_RC_ERROR, ME, "hostapd is NULL");
    ASSERTS_STR(pRad->hostapd->cfgFile, SWL_RC_ERROR, ME, "cfgFile is empty");
    wld_hostapd_config_t* config = NULL;
    bool ret = wld_hostapd_loadConfig(&config, pRad->hostapd->cfgFile);
    ASSERTS_TRUE(ret, SWL_RC_ERROR, ME, "Error loading config %s", pRad->Name);
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        if((pAP == NULL) || (pAP->pSSID == NULL)) {
            continue;
        }
        const char* configIdStr = wld_hostapd_getConfigParamByBssidValStr(config, (swl_macBin_t*) pAP->pSSID->BSSID, "config_id");
        if(configIdStr != NULL) {
            wld_ap_hostapd_setParamValue(pAP, "config_id", configIdStr, "refresh");
        }
    }
    wld_hostapd_deleteConfig(config);
    return SWL_RC_OK;
}

/**
 * @brief disable main hostapd interface
 *
 * @param pR radio context
 *
 * @return SWL_RC_OK when the the disabling cmd is sent. Otherwise error code.
 */
swl_rc_ne wld_rad_hostapd_disable(T_Radio* pR) {
    ASSERT_NOT_NULL(pR, SWL_RC_INVALID_PARAM, ME, "NULL");
    wld_wpaCtrlMngr_t* pMgr = wld_secDmn_getWpaCtrlMgr(pR->hostapd);
    wld_wpaCtrlInterface_t* pIface = wld_wpaCtrlMngr_getFirstReadyInterface(pMgr);
    ASSERT_NOT_NULL(pIface, SWL_RC_ERROR, ME, "%s: hostapd has no wpactrl iface ready", pR->Name);
    bool ret = wld_wpaCtrl_sendCmdCheckResponseExt(pIface, "DISABLE", "OK", 5000);
    ASSERTS_TRUE(ret, SWL_RC_ERROR, ME, "%s: disabling failed", wld_wpaCtrlInterface_getName(pIface));
    return SWL_RC_OK;
}

swl_trl_e wld_rad_hostapd_getCfgParamSupp(T_Radio* pRad, const char* param) {
    ASSERT_NOT_NULL(pRad, SWL_TRL_UNKNOWN, ME, "NULL");
    return wld_secDmn_getCfgParamSupp(pRad->hostapd, param);
}

T_AccessPoint* wld_rad_hostapd_getFirstConnectedVap(T_Radio* pRad) {
    if(pRad && pRad->enable) {
        T_AccessPoint* pAP = NULL;
        wld_rad_forEachAp(pAP, pRad) {
            if(pAP->enable &&
               wld_wpaCtrlInterface_checkConnectionPath(pAP->wpaCtrlInterface)) {
                return pAP;
            }
        }
    }
    return NULL;
}

T_AccessPoint* wld_rad_hostapd_getCfgMainVap(T_Radio* pRad) {
    T_AccessPoint* pMainAP = NULL;
    if(wld_rad_hasMloSupport(pRad) && !wld_rad_hasMbssidAds(pRad)) {
        if((pMainAP = wld_rad_hostapd_getFirstConnectedVap(pRad)) == NULL) {
            pMainAP = wld_rad_getFirstEnabledVap(pRad);
        }
    } else if(wld_rad_hasMbssidAds(pRad)) {
        pMainAP = wld_rad_getFirstEnabledVap(pRad);
    }
    if(pMainAP == NULL) {
        pMainAP = wld_rad_getFirstVap(pRad);
    }
    return pMainAP;
}

T_AccessPoint* wld_rad_hostapd_getRunMainVap(T_Radio* pRad) {
    char buf[64] = {0};
    swl_rc_ne rc = wld_rad_hostapd_getCmdReplyParamStr(pRad, "STATUS-DRIVER", "ifname", buf, sizeof(buf));
    ASSERTI_TRUE(swl_rc_isOk(rc), NULL, ME, "%s: fail to get main hapd iface name", pRad->Name);
    ASSERTI_STR(buf, NULL, ME, "%s: empty main hapd iface name", pRad->Name);
    T_AccessPoint* pAP = wld_vap_get_vap(buf);
    ASSERTI_NOT_NULL(pAP, NULL, ME, "%s: empty main hapd iface name", pRad->Name);
    if(pAP->pRadio == pRad) {
        return pAP;
    }
    /*
     * in case of MLD, fetch radio's main link vap with bssid
     * as main iface name is the MLD primary link iface
     */
    buf[0] = 0;
    rc = wld_rad_hostapd_getCmdReplyParamStr(pRad, "STATUS", "bssid[0]", buf, sizeof(buf));
    ASSERTI_TRUE(swl_rc_isOk(rc), NULL, ME, "%s: fail to get main hapd iface bssid", pRad->Name);
    swl_macBin_t bssid = SWL_MAC_BIN_NEW();
    ASSERTI_TRUE(swl_typeMacBin_fromChar(&bssid, buf), NULL, ME, "%s: invalid main bssid (%s)", pRad->Name, buf);
    ASSERTI_FALSE(swl_mac_binIsNull(&bssid), NULL, ME, "%s: null main bssid", pRad->Name);
    pAP = wld_ap_getVapByBssid(&bssid);
    return pAP;
}

swl_rc_ne wld_rad_hostapd_getCmdReplyParamStr(T_Radio* pRad, const char* cmd, const char* key, char* valStr, size_t valStrSize) {
    ASSERT_NOT_NULL(pRad, SWL_RC_INVALID_PARAM, ME, "NULL");
    wld_wpaCtrlMngr_t* pMgr = wld_secDmn_getWpaCtrlMgr(pRad->hostapd);
    wld_wpaCtrlInterface_t* pIface = wld_wpaCtrlMngr_getFirstReadyInterface(pMgr);
    return wld_wpaCtrl_getSyncCmdParamVal(pIface, cmd, key, valStr, valStrSize);
}

int32_t wld_rad_hostapd_getCmdReplyParam32Def(T_Radio* pRad, const char* cmd, const char* key, int32_t defVal) {
    char valStr[32] = {0};
    ASSERTS_TRUE(swl_rc_isOk(wld_rad_hostapd_getCmdReplyParamStr(pRad, cmd, key, valStr, sizeof(valStr))), defVal, ME, "Not found");
    ASSERTS_STR(valStr, defVal, ME, "Empty");
    int32_t valInt = defVal;
    ASSERTS_TRUE(swl_rc_isOk(wldu_convStrToNum(valStr, &valInt, sizeof(valInt), 10, true)), defVal, ME, "fail to convert");
    return valInt;
}

swl_rc_ne wld_rad_hostapd_getCfgParamStr(T_Radio* pRad, const char* key, char* valStr, size_t valStrSize) {
    ASSERTS_NOT_NULL(pRad, SWL_RC_INVALID_PARAM, ME, "NULL");
    ASSERTS_NOT_NULL(pRad->hostapd, SWL_RC_INVALID_STATE, ME, "NULL");
    ASSERTS_STR(key, SWL_RC_INVALID_PARAM, ME, "NULL");
    wld_hostapd_config_t* config = NULL;
    bool ret = wld_hostapd_loadConfig(&config, pRad->hostapd->cfgFile);
    ASSERTI_TRUE(ret, SWL_RC_ERROR, ME, "no saved config");
    ret = swl_str_copy(valStr, valStrSize, wld_hostapd_getConfigParamValStr(config, NULL, key));
    wld_hostapd_deleteConfig(config);
    ASSERTI_TRUE(ret, SWL_RC_ERROR, ME, "fail to copy result");
    return SWL_RC_OK;
}

int32_t wld_rad_hostapd_getCfgParamInt32Def(T_Radio* pRad, const char* key, int32_t defVal) {
    char valStr[32] = {0};
    ASSERTS_TRUE(swl_rc_isOk(wld_rad_hostapd_getCfgParamStr(pRad, key, valStr, sizeof(valStr))), defVal, ME, "Not found");
    ASSERTS_STR(valStr, defVal, ME, "Empty");
    int32_t valInt = defVal;
    ASSERTS_TRUE(swl_rc_isOk(wldu_convStrToNum(valStr, &valInt, sizeof(valInt), 10, true)), defVal, ME, "fail to convert");
    return valInt;
}

swl_channel_t wld_rad_hostapd_getCfgChannel(T_Radio* pRad) {
    return wld_rad_hostapd_getCfgParamInt32Def(pRad, "channel", SWL_CHANNEL_INVALID);
}

swl_chanspec_t wld_rad_hostapd_getCfgChanspec(T_Radio* pRad) {
    swl_channel_t channel = wld_rad_hostapd_getCfgChannel(pRad);
    uint32_t opClass = wld_rad_hostapd_getCfgParamInt32Def(pRad, "op_class", 0);
    return swl_chanspec_fromOperClass(opClass, SWL_OP_CLASS_COUNTRY_GLOBAL, channel);
}


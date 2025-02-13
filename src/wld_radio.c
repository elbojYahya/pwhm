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

#define ME "rad"

#include <stdlib.h>
#include <debug/sahtrace.h>



#include <string.h>
#include <assert.h>
#include <ctype.h>

#include <swla/swla_commonLib.h>
#include <swla/swla_conversion.h>
#include <swl/swl_string.h>

#include "wld.h"
#include "wld_util.h"
#include "wld_radio.h"
#include "wld_accesspoint.h"
#include "wld_ssid.h"
#include "wld_endpoint.h"
#include "wld_wps.h"
#include "wld_channel.h"
#include "wld_chanmgt.h"
#include "swl/swl_assert.h"
#include "swla/swla_oui.h"
#include "swl/swl_hex.h"
#include "wld_statsmon.h"
#include "wld_channel_types.h"
#include "wld_ap_rssiMonitor.h"
#include "wld_rad_stamon.h"
#include "wld_assocdev.h"
#include "wld_eventing.h"
#include "Utils/wld_autoCommitMgr.h"
#include "wld/Utils/wld_autoNeighAdd.h"

#include "wld_hostapd_cfgFile.h"
#include "wld_rad_nl80211.h"
#include "wld_linuxIfStats.h"
#include "wld_linuxIfUtils.h"
#include "wld_dm_trans.h"
#include <swl/swl_base64.h>
#include "swla/swla_trans.h"
#include "wld_extMod.h"

#define GETENV(x, var) \
    { \
        char* str = getenv(var); \
        int __ret = snprintf(x, sizeof(x), "%s", str ? str : ""); \
        if(__ret < 0 || __ret >= (int) sizeof(x)) { \
            x[0] = 0;} \
    }

const char* Rad_SupStatus[] = {"Error", "LowerLayerDown", "NotPresent", "Dormant", "Unknown", "Down", "Up", 0};
const char* wld_status_str[] = {"Error", "LowerLayerDown", "NotPresent", "Dormant", "Unknown", "Down", "Up", 0};

const char** Rad_SupFreqBands = swl_freqBandExt_str;

const char** Rad_SupBW = swl_bandwidth_str;

const char* Rad_RadarZones[RADARZONES_MAX] = {"NONE", "ETSI", "STG", "UNCLASSIFIED", "FCC", "JP"};

const char* Rad_SupCExt[] = {"Auto", "AboveControlChannel", "BelowControlChannel", "None", NULL};
const char* Rad_SupGI[] = {"Auto", "400nsec", "800nsec", 0};

const char* wld_rad_autoBwSelectMode_str[] = {"MaxAvailable", "MaxCleared", "Default", NULL};

const char* cstr_chanmgt_rad_state[CM_RAD_MAX + 1] = {
    "Unknown",
    "Down",
    "Up",
    "FG_CAC",
    "BG_CAC",
    "BG_CAC_EXT",
    "BG_CAC_NS",
    "BG_CAC_EXT_NS",
    "Configuring",
    "DeepPowerDown",
    "DelayApUp",
    "Error",
    NULL
};

const char* g_str_wld_he_cap[HE_CAP_MAX] = {
    "DEFAULT",
    "DL_OFDMA",
    "UL_OFDMA",
    "DL_MUMIMO",
    "UL_MUMIMO",
    "STA_UL_OFDMA",
    "STA_UL_MUMIMO",
    "HE_ER_SU_PPDU_RX"
};

const char* g_str_wld_rad_bf_cap[RAD_BF_CAP_MAX] = {
    "DEFAULT",
    "VHT_SU_BF",
    "VHT_MU_BF",
    "HE_SU_BF",
    "HE_MU_BF",
    "HE_CQI_BF",
    "EHT_SU_BF",
    "EHT_MU_80_BF",
    "EHT_MU_160_BF",
    "EHT_MU_320_BF",
};

const char* g_wld_channelChangeReason_str[CHAN_REASON_MAX + 1] = {
    "INVALID",
    "UNKNOWN",
    "INITIAL",
    "PERSISTANCE",
    "AUTO",
    "DFS",
    "MANUAL",
    "REENTRY",
    "OBSS_COEX",
    "EP_MOVE",
    "RESET",
    NULL
};

const char* g_str_wld_tpcMode[TPCMODE_MAX] = {
    "Off",
    "Sta",
    "Ap",
    "ApSta",
    "Auto",
};

static const char* Rad_RIFS_MODE[RIFS_MODE_MAX] = {"Default", "Auto", "Off", "On"};

static const char* g_wld_radFeatures_str[WLD_FEATURE_MAX] = {"Seamless DFS", "Background DFS"};

static const char* g_wld_preambleType[PREAMBLE_TYPE_MAX] = {"short", "long", "auto"};

static const char* g_wld_mbssidAdvertisementMode[MBSSID_ADVERTISEMENT_MODE_MAX] = {"Auto", "Off", "On", "Enhanced"};

amxc_llist_t g_radios = { NULL, NULL };

SWL_ARRAY_TYPE_C(gtWld_type_statusArray, gtSwl_type_uint32, RST_MAX, true, true);
SWL_NTT_C(gtWld_status_changeInfo, wld_status_changeInfo_t, X_WLD_STATUS_CHANGE_INFO);

const char* radCounterDefaults[WLD_RAD_EV_MAX] = {
    "0",
    "0",
    "",
};

static amxd_status_t _linkFirstUinitRadio(amxd_object_t* pRadioObj, swl_freqBandExt_e band, int32_t wiPhyId) {

    ASSERT_NOT_NULL(pRadioObj, amxd_status_unknown_error, ME, "NULL");
    T_Radio* pRad = wld_getRadioByWiPhyId(wiPhyId);

    if((pRad == NULL) || (!SWL_BIT_IS_SET(pRad->supportedFrequencyBands, band))) {
        pRad = wld_getUinitRadioByBand(band);
    }
    ASSERTW_NOT_NULL(pRad, amxd_status_unknown_error, ME, "No uninit pRad for frequency %s", swl_freqBandExt_str[band]);

    ASSERT_NULL(pRadioObj->priv, amxd_status_unknown_error, ME, "pRadioObj->priv not NULL %s", pRad->Name);

    swl_str_copy(pRad->instanceName, sizeof(pRad->instanceName), amxd_object_get_name(pRadioObj, AMXD_OBJECT_NAMED));
    SAH_TRACEZ_WARNING(ME, "Mapping new Radio instance [%s:%s]", pRad->Name, pRad->instanceName);

    /* General initializations */
    pRadioObj->priv = pRad;
    pRad->pBus = pRadioObj;

    /*
     * init the radio base mac with radio object instance index
     * instead of radio device detection order
     */
    wld_initRadioBaseMac(pRad, amxd_object_get_index(pRadioObj) - 1);

    return amxd_status_ok;
}

void wld_rad_setAllMldLinksUnconfigured(T_Radio* pR) {
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    T_AccessPoint* pAP = NULL;
    wld_rad_forEachAp(pAP, pR) {
        if(pAP->pSSID != NULL) {
            wld_mld_setLinkConfigured(pAP->pSSID->pMldLink, false);
        }
    }
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pR) {
        if(pEP->pSSID != NULL) {
            wld_mld_setLinkConfigured(pEP->pSSID->pMldLink, false);
        }
    }
}

static void s_setEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool flag = amxc_var_dyncast(bool, newValue);
    ASSERTI_NOT_EQUALS(pR->enable, flag, , ME, "%s: same enable %d", pR->Name, flag);
    SAH_TRACEZ_INFO(ME, "%s: set Enable %d --> %d", pR->Name, pR->enable, flag);
    pR->pFA->mfn_wrad_enable(pR, flag, SET);

    wld_autoCommitMgr_notifyRadEdit(pR);

    if(flag) {
        pR->changeInfo.nrEnables++;
        pR->changeInfo.lastEnableTime = swl_time_getMonoSec();
    } else {
        pR->changeInfo.lastDisableTime = swl_time_getMonoSec();
    }

    wld_rad_setAllMldLinksUnconfigured(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setKickRoamSta_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool flag = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: setKickRoamSta %d", pR->Name, flag);
    pR->kickRoamStaEnabled = flag;
    pR->fsmRad.FSM_SyncAll = TRUE;
    wld_autoCommitMgr_notifyRadEdit(pR);
    SAH_TRACEZ_OUT(ME);
}

amxd_status_t _wld_rad_getSupportedFrequencyBands_prf(amxd_object_t* object,
                                                      amxd_param_t* param,
                                                      amxd_action_t reason,
                                                      const amxc_var_t* const args _UNUSED,
                                                      amxc_var_t* const retval,
                                                      void* priv _UNUSED) {
    ASSERTS_NOT_NULL(param, amxd_status_unknown_error, ME, "NULL");
    ASSERTS_EQUALS(reason, action_param_read, amxd_status_function_not_implemented, ME, "not impl");
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, amxd_status_ok, ME, "no radio mapped");

    SAH_TRACEZ_IN(ME);
    char TBuf[128] = {0};
    swl_conv_maskToChar(TBuf, sizeof(TBuf), pR->supportedFrequencyBands, swl_freqBandExt_str, SWL_FREQ_BAND_MAX);
    // Update with potential vendor info
    char* pSFB = wld_getVendorParam(pR, "SupportedFrequencyBands", TBuf);
    amxc_var_set(cstring_t, retval, pSFB);
    free(pSFB);
    SAH_TRACEZ_OUT(ME);
    return amxd_status_ok;
}

amxd_status_t _wld_rad_validateChannel_pvf(amxd_object_t* object _UNUSED,
                                           amxd_param_t* param,
                                           amxd_action_t reason _UNUSED,
                                           const amxc_var_t* const args,
                                           amxc_var_t* const retval _UNUSED,
                                           void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    T_Radio* pRad = (T_Radio*) object->priv;
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    /*
     * wait for all rad conf be loaded, and especially OperFreq and RegDomain
     * to have the right list of possible channels
     * against which the new channel is checked
     */
    ASSERTI_TRUE(pRad->hasDmReady, amxd_status_ok, ME, "%s: radio config not yet fully loaded", pRad->Name);
    uint32_t currentValue = amxc_var_dyncast(uint32_t, &param->value);
    uint32_t newValue = amxc_var_dyncast(uint32_t, args);
    if((currentValue == newValue) || (wld_rad_hasChannel(pRad, newValue))) {
        return amxd_status_ok;
    }
    SAH_TRACEZ_ERROR(ME, "%s: invalid channel %d", pRad->Name, newValue);
    return amxd_status_invalid_value;
}

static void s_setChannel_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pR, , ME, "No radio mapped");
    swl_channel_t channel = amxc_var_dyncast(int32_t, newValue);
    if((channel == pR->channel) &&
       (!swl_typeChanspec_equals(wld_chanmgt_getTgtChspec(pR), (swl_chanspec_t) SWL_CHANSPEC_EMPTY))) {
        SAH_TRACEZ_INFO(ME, "%s: Same channel %d", pR->Name, channel);
        return;
    }


    SAH_TRACEZ_INFO(ME, "%s: set RadioChannel %d", pR->Name, channel);
    SAH_TRACEZ_INFO(ME, "STATUS %d %d %d",
                    pR->channel,
                    pR->autoChannelEnable,
                    pR->autoChannelSetByUser);

    if(channel) {
        /**
         * Find if any EP from the radio is connected in order
         * to avoid any channel that may lose connection with
         * AP
         */
        T_EndPoint* ep = wld_rad_getRunningEndpoint(pR);
        if(ep != NULL) {
            SAH_TRACEZ_INFO(ME, "%s: EP %s connected, not change channel", pR->Name, ep->Name);
            SAH_TRACEZ_OUT(ME);
            return;
        }

        /* Disable autochannel if valid channel and first commit has been done
         * I.e. use case where user writes channel manually.
         */
        if(pR->autoChannelEnable && wld_rad_firstCommitFinished(pR)) {
            SAH_TRACEZ_WARNING(ME, "%s: disable autochan from chan config", pR->Name);
            swl_typeUInt8_commitObjectParam(pR->pBus, "AutoChannelEnable", 0);
        }

        pR->channelChangeReason = CHAN_REASON_MANUAL;
        swl_chanspec_t chanspec = swl_chanspec_fromDm(channel, pR->operatingChannelBandwidth, pR->operatingFrequencyBand);
        swl_rc_ne retcode = wld_chanmgt_setTargetChanspec(pR, chanspec, false, CHAN_REASON_MANUAL, NULL);
        pR->userChanspec = pR->targetChanspec.chanspec;
        if(retcode < SWL_RC_OK) {
            /* If target channel is not valid, reset channel entry */
            wld_rad_chan_update_model(pR, NULL);
        }

        wld_autoCommitMgr_notifyRadEdit(pR);
    } else {
        SAH_TRACEZ_WARNING(ME, "%s: enable autochan from chan config", pR->Name);
        swl_typeUInt8_commitObjectParam(pR->pBus, "AutoChannelEnable", 1);
    }

    SAH_TRACEZ_OUT(ME);
}

static void s_setAPMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool APMode = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set AP_Mode %d", pR->Name, APMode);
    pR->isAP = APMode;
    pR->fsmRad.FSM_SyncAll = TRUE;
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_finalizeEPs(T_Radio* pR) {
    bool needRadRestart = false;
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pR) {
        int prevNDevIdx = pEP->index;
        if(wld_endpoint_needCreateIface(pEP) || wld_endpoint_needDestroyIface(pEP)) {
            wld_endpoint_finalize(pEP);
        }
        if(prevNDevIdx != pEP->index) {
            SAH_TRACEZ_INFO(ME, "%s: radIdx(%d) prevEpIdx(%d) currEpIdx(%d) => needRadRestart(%d)",
                            pEP->alias, pR->index, prevNDevIdx, pEP->index, needRadRestart);
            // restart the whole radio fsm if the added/removed endpoint matches the radio netdevIdx
            needRadRestart |= ((prevNDevIdx == pR->index) || (pEP->index == pR->index));
        }
    }
    if(needRadRestart) {
        pR->fsmRad.FSM_SyncAll = TRUE;
        wld_autoCommitMgr_notifyRadEdit(pR);
    }
}

static void s_setSTAMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool STAMode = amxc_var_dyncast(bool, newValue);
    ASSERT_NOT_EQUALS(pR->isSTA, STAMode, , ME, "same value");
    SAH_TRACEZ_INFO(ME, "%s: set STA_Mode %d", pR->Name, STAMode);
    pR->isSTA = STAMode;
    //just need to check the complementary condition for EP interface finalization
    if(pR->isSTASup) {
        swla_delayExec_add((swla_delayExecFun_cbf) s_finalizeEPs, pR);
    }

    SAH_TRACEZ_OUT(ME);
}

static void s_setWDSMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool wdsMode = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set WDS_Mode %d", pR->Name, wdsMode);
    pR->isWDS = wdsMode;
    pR->fsmRad.FSM_SyncAll = TRUE;
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setWETMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool wetMode = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set WET_Mode %d", pR->Name, wetMode);
    pR->isWET = wetMode;
    pR->fsmRad.FSM_SyncAll = TRUE;
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setStaSupMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool StaSupMode = amxc_var_dyncast(bool, newValue);
    ASSERT_NOT_EQUALS(pR->isSTASup, StaSupMode, , ME, "same value");
    SAH_TRACEZ_INFO(ME, "%s: set STASup_Mode %d", pR->Name, StaSupMode);
    pR->isSTASup = StaSupMode;
    if(pR->isSTA) {
        swla_delayExec_add((swla_delayExecFun_cbf) s_finalizeEPs, pR);
    }
    SAH_TRACEZ_OUT(ME);
}

// Chicken Egg issue with WPS when both STA and AP have WPS set...
// This radio setting will decide how we will manage this!
static void s_setWPSEnrolMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool WPS_EnrolleeMode = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set WPSEnrol_Mode %d", pR->Name, WPS_EnrolleeMode);
    pR->isWPSEnrol = WPS_EnrolleeMode;
    pR->fsmRad.FSM_SyncAll = TRUE;
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setExternalAcsMgmt_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");

    pRad->externalAcsMgmt = amxc_var_dyncast(bool, newValue);

    SAH_TRACEZ_INFO(ME, "%s: External ACS management: enable=%d", pRad->Name, pRad->externalAcsMgmt);

    SAH_TRACEZ_OUT(ME);
}

static void s_setAutoChannelEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");

    bool newAutochan = amxc_var_dyncast(bool, newValue);

    SAH_TRACEZ_INFO(ME, "%s: ACS STATUS PRE  %d %d %d => val %u",
                    pRad->Name,
                    pRad->channel,
                    pRad->autoChannelEnable,
                    pRad->autoChannelSetByUser,
                    newAutochan);

    if(pRad->externalAcsMgmt) {
        SAH_TRACEZ_WARNING(ME, "%s: External ACS management: ACS enable=%d", pRad->Name, newAutochan);
        pRad->autoChannelSetByUser = newAutochan;
        return;
    }

    swl_rc_ne ret = pRad->pFA->mfn_wrad_autochannelenable(pRad, newAutochan, SET);
    wld_autoCommitMgr_notifyRadEdit(pRad);

    if(ret == SWL_RC_NOT_IMPLEMENTED) {
        pRad->autoChannelEnable = newAutochan;
    }


    SAH_TRACEZ_INFO(ME, "%s: ACS STATUS POST %d %d %d : ret %d",
                    pRad->Name,
                    pRad->channel,
                    pRad->autoChannelEnable,
                    pRad->autoChannelSetByUser,
                    ret);

    SAH_TRACEZ_OUT(ME);
}

amxd_status_t _wld_rad_validateOperatingChannelBandwidth_pvf(amxd_object_t* object,
                                                             amxd_param_t* param,
                                                             amxd_action_t reason _UNUSED,
                                                             const amxc_var_t* const args,
                                                             amxc_var_t* const retval _UNUSED,
                                                             void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    amxd_status_t status = amxd_status_invalid_value;
    T_Radio* pRad = (T_Radio*) object->priv;
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    const char* currentValue = amxc_var_constcast(cstring_t, &param->value);
    ASSERT_NOT_NULL(currentValue, status, ME, "NULL");
    char* newValue = amxc_var_dyncast(cstring_t, args);
    ASSERT_NOT_NULL(newValue, status, ME, "NULL");
    swl_radBw_e radBw = swl_conv_charToEnum(newValue, swl_radBw_str, SWL_RAD_BW_MAX, SWL_RAD_BW_MAX);

    if((swl_str_matches(currentValue, newValue)) ||
       ((radBw < SWL_RAD_BW_MAX) &&
        ((radBw == SWL_RAD_BW_AUTO) || (swl_chanspec_radBwToInt(radBw) <= swl_chanspec_bwToInt(pRad->maxChannelBandwidth))))) {
        status = amxd_status_ok;
    } else {
        SAH_TRACEZ_ERROR(ME, "%s: unsupported operating channel bandwidth(%s / %u / %u)", pRad->Name, newValue,
                         swl_chanspec_radBwToInt(radBw), swl_chanspec_bwToInt(pRad->maxChannelBandwidth));
    }
    free(newValue);
    return status;
}

static void s_setOperatingChannelBandwidth_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, , ME, "NULL");
    const char* OCBW = amxc_var_constcast(cstring_t, newValue);

    swl_radBw_e radBw = swl_conv_charToEnum(OCBW, swl_radBw_str, SWL_RAD_BW_MAX, SWL_RAD_BW_AUTO);
    if((radBw == pRad->operatingChannelBandwidth) && (radBw == pRad->runningChannelBandwidth) &&
       (!swl_typeChanspec_equals(wld_chanmgt_getTgtChspec(pRad), (swl_chanspec_t) SWL_CHANSPEC_EMPTY))) {
        SAH_TRACEZ_INFO(ME, "%s: Same bandwidth %s", pRad->Name, swl_radBw_str[radBw]);
        return;
    }
    bool autoBwChange = (pRad->operatingChannelBandwidth == SWL_RAD_BW_AUTO || radBw == SWL_RAD_BW_AUTO);

    pRad->operatingChannelBandwidth = radBw;
    pRad->channelBandwidthChangeReason = CHAN_REASON_MANUAL;

    swl_chanspec_t chanspec = swl_chanspec_fromDm(pRad->channel, radBw, pRad->operatingFrequencyBand);
    wld_chanmgt_setTargetChanspec(pRad, chanspec, false, CHAN_REASON_MANUAL, NULL);
    pRad->userChanspec = pRad->targetChanspec.chanspec;
    SAH_TRACEZ_INFO(ME, "%s: set OCBW %s : %u", pRad->Name, swl_radBw_str[radBw], autoBwChange);

    if(autoBwChange) {
        // if autobandwidth is enabled, and going from or to autobw, trigger autochannelEnable
        pRad->pFA->mfn_wrad_autochannelenable(pRad, pRad->autoChannelEnable, SET);
    }
    wld_autoCommitMgr_notifyRadEdit(pRad);

    SAH_TRACEZ_OUT(ME);
}

static void s_setAutoBandwidthSelectMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pRad, , ME, "NULL");
    const char* TCBW = amxc_var_constcast(cstring_t, newValue);
    wld_rad_bwSelectMode_e autoBwSelectMode = swl_conv_charToEnum(TCBW, wld_rad_autoBwSelectMode_str, BW_SELECT_MODE_MAX, BW_SELECT_MODE_DEFAULT);
    SAH_TRACEZ_INFO(ME, "%s: set AutoBandwidthSelectMode %s %d", pRad->Name, TCBW, autoBwSelectMode);
    ASSERTI_NOT_EQUALS(pRad->autoBwSelectMode, autoBwSelectMode, , ME, "same value");
    pRad->autoBwSelectMode = autoBwSelectMode;
    ASSERTI_EQUALS(pRad->operatingChannelBandwidth, SWL_RAD_BW_AUTO, , ME, "%s: not auto bw", pRad->Name);
    ASSERTI_FALSE(pRad->autoChannelEnable, , ME, "%s: let auto channel enabled control bandwidth", pRad->Name);
    swl_chanspec_t chanspec = swl_chanspec_fromDm(pRad->channel, pRad->operatingChannelBandwidth, pRad->operatingFrequencyBand);
    SAH_TRACEZ_INFO(ME, "%s: set target chanspec %s with new AutoBWMode %s",
                    pRad->Name, swl_typeChanspecExt_toBuf32(chanspec).buf, wld_rad_autoBwSelectMode_str[autoBwSelectMode]);
    wld_chanmgt_setTargetChanspec(pRad, chanspec, false, CHAN_REASON_MANUAL, NULL);
    pRad->userChanspec = pRad->targetChanspec.chanspec;
    wld_autoCommitMgr_notifyRadEdit(pRad);

    SAH_TRACEZ_OUT(ME);
}

static void s_setObssCoexistenceEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    bool flag = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set ObssCoexistenceEnable %d", pR->Name, flag);
    ASSERTS_NOT_EQUALS(pR->obssCoexistenceEnabled, flag, , ME, "same");
    pR->obssCoexistenceEnabled = flag;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setExtensionChannel_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    const char* valStr = amxc_var_constcast(cstring_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set ExtensionChannel %s", pR->Name, valStr);
}

static void checkRadioEventLogLimitReached (T_Radio* pR) {
    SAH_TRACEZ_IN(ME);

    ASSERT_NOT_NULL(pR, , ME, "NULL");
    amxd_object_t* evt = amxd_object_findf(pR->pBus, "DFS.Event");
    amxc_llist_it_t* it = amxd_object_first_instance(evt);
    amxd_object_t* chld = amxc_llist_it_get_data(it, amxd_object_t, it);
    while(pR->dfsEventNbr > pR->dfsEventLogLimit) {
        ASSERT_NOT_NULL(chld, , ME, "NULL");
        swl_object_delInstWithTransOnLocalDm(chld);
        amxc_llist_it_t* it = amxd_object_first_instance(chld);
        chld = amxc_llist_it_get_data(it, amxd_object_t, it);
        pR->dfsEventNbr--;
    }

    SAH_TRACEZ_OUT(ME);
}

bool wld_rad_addDFSEvent(T_Radio* pR, T_DFSEvent* evt) {
    ASSERT_NOT_NULL(pR, false, ME, "NULL");

    amxd_object_t* event = amxd_object_findf(pR->pBus, "DFS.Event");
    ASSERT_NOT_NULL(event, false, ME, "NULL");


    amxd_trans_t trans;
    ASSERT_TRANSACTION_INIT(event, &trans, SWL_RC_ERROR, ME, "%s : trans init failure", pR->Name);

    amxd_trans_add_inst(&trans, 0, NULL);

    swl_typeTimeReal_toTransParam(&trans, "TimeStamp", evt->timestamp);

    amxd_trans_set_uint32_t(&trans, "Channel", evt->channel);
    amxd_trans_set_cstring_t(&trans, "Bandwidth", Rad_SupBW[evt->bandwidth]);
    amxd_trans_set_cstring_t(&trans, "RadarZone", Rad_RadarZones[evt->radarZone]);
    amxd_trans_set_uint8_t(&trans, "RadarIndex", evt->radarIndex);

    amxd_trans_set_uint32_t(&trans, "NewChannel", pR->channel);
    amxd_trans_set_cstring_t(&trans, "NewBandwidth", swl_radBw_str[pR->runningChannelBandwidth]);

    swl_typeUInt8_arrayTransParamSetChar(&trans, "DFSRadarDetectionList",
                                         pR->lastRadarChannelsAdded, pR->nrLastRadarChannelsAdded);

    ASSERT_TRANSACTION_LOCAL_DM_END(&trans, SWL_RC_ERROR, ME, "%s : trans apply failure", pR->Name);


    pR->dfsEventNbr++;
    checkRadioEventLogLimitReached(pR);

    return true;
}

swl_chanspec_t wld_rad_getSwlChanspec(T_Radio* pRad) {
    swl_chanspec_t chanspec = SWL_CHANSPEC_EMPTY;
    ASSERTS_NOT_NULL(pRad, chanspec, ME, "NULL");
    chanspec = swl_chanspec_fromDm(pRad->channel, pRad->runningChannelBandwidth, pRad->operatingFrequencyBand);
    return chanspec;
}

/*
 * @brief get current radio noise level (in dbm) using air statistics handler
 *
 * @param pRadio pointer to radio context
 * @param pNoise pointer to result noise
 *
 * @return SWL_RC_OK in case of success
 *         <= SWL_RC_ERROR otherwise
 */
swl_rc_ne wld_rad_getCurrentNoise(T_Radio* pRad, int32_t* pNoise) {
    ASSERT_NOT_NULL(pRad, SWL_RC_INVALID_PARAM, ME, "NULL");
    ASSERT_NOT_NULL(pNoise, SWL_RC_INVALID_PARAM, ME, "NULL");
    ASSERTI_TRUE(wld_rad_isActive(pRad), SWL_RC_INVALID_STATE, ME, "%s : not ready", pRad->Name);

    *pNoise = 0;
    wld_airStats_t airStats;
    memset(&airStats, 0, sizeof(airStats));
    swl_rc_ne rc = pRad->pFA->mfn_wrad_airstats(pRad, &airStats);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "%s: fail to get air stats", pRad->Name);
    *pNoise = airStats.noise;
    return SWL_RC_OK;
}

static void s_setGuardInterval_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    char* giStr = amxc_var_dyncast(cstring_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set GuardInterval %s", pR->Name, giStr);
    swl_enum_e gi = swl_conv_charToEnum(giStr, Rad_SupGI, SWL_ARRAY_SIZE(Rad_SupGI), SWL_SGI_AUTO);
    free(giStr);
    ASSERTS_NOT_EQUALS(pR->guardInterval, gi, , ME, "EQUAL");
    pR->guardInterval = gi;
    pR->pFA->mfn_wrad_guardintval(pR, NULL, pR->guardInterval, SET);

    SAH_TRACEZ_OUT(ME);
}

static swl_rc_ne s_updateRadioStatsValues(amxd_object_t* const stats) {
    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(stats));
    ASSERT_NOT_NULL(pR, SWL_RC_INVALID_PARAM, ME, "NULL");

    if(!(wld_rad_hasActiveVap(pR) || wld_rad_hasRunningEndpoint(pR)) || (pR->pFA->mfn_wrad_stats(pR) < 0)) {
        // Update the stats with Linux counters if we don't handle them in the plugin.
        wld_updateRadioStats(pR, NULL);
    }

    ASSERT_NOT_NULL(stats, SWL_RC_INVALID_PARAM, ME, "stats NULL");
    wld_util_stats2Obj((amxd_object_t*) stats, &pR->stats);
    return SWL_RC_OK;
}

amxd_status_t _getRadioStats(amxd_object_t* object,
                             amxd_function_t* func _UNUSED,
                             amxc_var_t* args _UNUSED,
                             amxc_var_t* retval) {

    amxd_object_t* stats = amxd_object_get(object, "Stats");

    // Update the stats object
    swl_rc_ne rc = s_updateRadioStatsValues(stats);
    ASSERT_TRUE(swl_rc_isOk(rc), amxd_status_unknown_error, ME, "fail to update radio stats");

    // copy stats object to returned variant
    amxc_var_set_type(retval, AMXC_VAR_ID_HTABLE);
    wld_util_statsObj2Var(retval, stats);

    return amxd_status_ok;
}

amxd_status_t _wld_radio_getStats_orf(amxd_object_t* const object,
                                      amxd_param_t* const param,
                                      amxd_action_t reason,
                                      const amxc_var_t* const args,
                                      amxc_var_t* const action_retval,
                                      void* priv) {
    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, amxd_status_unknown_error, ME, "No mapped radio");
    return swla_dm_procObjActionRead(object, param, reason, args, action_retval, priv, &pR->onReadStatsCtx, s_updateRadioStatsValues);
}

amxd_status_t _wld_rad_getChannelLoad_prf(amxd_object_t* object,
                                          amxd_param_t* param,
                                          amxd_action_t reason,
                                          const amxc_var_t* const args _UNUSED,
                                          amxc_var_t* const retval,
                                          void* priv _UNUSED) {
    ASSERTS_NOT_NULL(param, amxd_status_unknown_error, ME, "NULL");
    uint16_t channelLoad = 0;
    ASSERTS_EQUALS(reason, action_param_read, amxd_status_function_not_implemented, ME, "not impl");
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, amxd_status_unknown_error, ME, "no radio mapped");
    ASSERTI_TRUE(wld_rad_isActive(pR), amxd_status_ok, ME, "%s : not ready", pR->Name);

    SAH_TRACEZ_IN(ME);
    wld_airStats_t airStats = {0};
    swl_rc_ne rc = pR->pFA->mfn_wrad_airstats(pR, &airStats);
    if(rc == SWL_RC_OK) {
        channelLoad = airStats.load;
    }
    amxc_var_set(uint16_t, retval, channelLoad);
    SAH_TRACEZ_OUT(ME);
    return amxd_status_ok;
}

static swl_rc_ne s_getTxPowerPct(T_Radio* pR, int32_t* txPwrPct) {
    ASSERTS_NOT_NULL(pR, SWL_RC_INVALID_PARAM, ME, "NULL");
    ASSERTS_NOT_NULL(txPwrPct, SWL_RC_INVALID_PARAM, ME, "NULL");

    swl_rc_ne rc = SWL_RC_OK;
    int32_t percentage = pR->pFA->mfn_wrad_txpow(pR, 0, GET);
    if((percentage < -1) || (percentage > 100)) {
        SAH_TRACEZ_WARNING(ME, "%s: out of range txPwr prc %d, consider auto mode", pR->Name, percentage);
        percentage = -1;
        rc = SWL_RC_ERROR;
    }

    *txPwrPct = percentage;

    return rc;
}

amxd_status_t _wld_rad_validateTxPower_pvf(amxd_object_t* object,
                                           amxd_param_t* param,
                                           amxd_action_t reason _UNUSED,
                                           const amxc_var_t* const args,
                                           amxc_var_t* const retval _UNUSED,
                                           void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    int8_t currentValue = amxc_var_dyncast(int8_t, &param->value);
    int8_t newValue = amxc_var_dyncast(int8_t, args);
    ASSERTS_NOT_EQUALS(currentValue, newValue, amxd_status_ok, ME, "same value");
    for(size_t i = 0; i < SWL_ARRAY_SIZE(pRad->transmitPowerSupported); i++) {
        if(pRad->transmitPowerSupported[i] == newValue) {
            return amxd_status_ok;
        }
    }
    SAH_TRACEZ_ERROR(ME, "%s: unsupported txPwr percentage value(%d)", pRad->Name, newValue);
    return amxd_status_invalid_value;
}

static void s_setTxPower_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    int32_t power = amxc_var_dyncast(int32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set TxPower %d", pR->Name, power);
    pR->pFA->mfn_wrad_txpow(pR, power, SET);

    SAH_TRACEZ_OUT(ME);
}

static void s_setDriverConfig_ocf(void* priv _UNUSED, amxd_object_t* object, const amxc_var_t* const newParamValues _UNUSED) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERTI_NOT_NULL(pRad, , ME, "NULL");
    SAH_TRACEZ_INFO(ME, "%s: update driver config", pRad->Name);

    pRad->driverCfg.txBurst = amxd_object_get_int32_t(object, "TxBurst", NULL);
    pRad->driverCfg.ampdu = amxd_object_get_int32_t(object, "Ampdu", NULL);
    pRad->driverCfg.amsdu = amxd_object_get_int32_t(object, "Amsdu", NULL);
    pRad->driverCfg.fragThreshold = amxd_object_get_int32_t(object, "FragmentationThreshold", NULL);
    pRad->driverCfg.rtsThreshold = amxd_object_get_int32_t(object, "RtsThreshold", NULL);
    pRad->driverCfg.txBeamforming = amxd_object_get_int32_t(object, "TxBeamforming", NULL);
    pRad->driverCfg.vhtOmnEnabled = amxd_object_get_bool(object, "VhtOmnEnabled", NULL);
    pRad->driverCfg.broadcastMaxBwCapability = amxd_object_get_int32_t(object, "BroadcastMaxBwCapability", NULL);
    pRad->driverCfg.tpcMode = swl_conv_objectParamEnum(object, "TPCMode", g_str_wld_tpcMode, TPCMODE_MAX, TPCMODE_OFF);
    wld_rad_doSync(pRad);

    SAH_TRACEZ_OUT(ME);
}

SWLA_DM_HDLRS(sDriverConfigDmHdlrs, ARR(), .objChangedCb = s_setDriverConfig_ocf);

void _wld_rad_setDriverConfig_ocf(const char* const sig_name,
                                  const amxc_var_t* const data,
                                  void* const priv) {
    swla_dm_procObjEvtOfLocalDm(&sDriverConfigDmHdlrs, sig_name, data, priv);
}

static void s_setAntennaCtrl_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);
    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    int32_t antenna = amxc_var_dyncast(int32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set AntennaCtrl %i", pR->Name, antenna);
    pR->actAntennaCtrl = antenna;
    pR->pFA->mfn_wrad_antennactrl(pR, antenna, SET);

    SAH_TRACEZ_OUT(ME);
}

static void s_setRxChainCtrl_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    int32_t antenna = amxc_var_dyncast(int32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set RxChainCtrl %i", pR->Name, antenna);
    pR->rxChainCtrl = antenna;
    pR->pFA->mfn_wrad_antennactrl(pR, pR->actAntennaCtrl, SET);

    SAH_TRACEZ_OUT(ME);
}

static void s_setTxChainCtrl_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    int32_t antenna = amxc_var_dyncast(int32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set TxChainCtrl %i", pR->Name, antenna);
    pR->txChainCtrl = antenna;
    pR->pFA->mfn_wrad_antennactrl(pR, pR->actAntennaCtrl, SET);

    SAH_TRACEZ_OUT(ME);
}

/**
 * Enables debugging on third party software modules.
 * The goal of this is only for developpers!
 * We need a way to have/enable a better view how things are managed by some deamons.
 */
static void s_setDbgEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    int dbgRadEnable = amxc_var_dyncast(int32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set dbgEnable %d", pR->Name, dbgRadEnable);
    pR->dbgEnable = dbgRadEnable;

    SAH_TRACEZ_OUT(ME);
}

static void s_setDbgFile_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    const char* dbgFile = amxc_var_constcast(cstring_t, newValue);
    ASSERT_NOT_NULL(dbgFile, , ME, "NULL");
    SAH_TRACEZ_INFO(ME, "%s: set DbgFile %s", pR->Name, dbgFile);
    swl_str_copyMalloc(&pR->dbgOutput, dbgFile);

    SAH_TRACEZ_OUT(ME);
}

/* Setting Retry Limit */
static void s_setRetryLimit_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    uint8_t retryLimitValue = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set RetryLimit %d", pR->Name, retryLimitValue);
    pR->retryLimit = retryLimitValue;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setRtsThreshold_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    uint16_t rtsThreshold = amxc_var_dyncast(uint16_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set RTS threshold %u", pRad->Name, rtsThreshold);
    pRad->rtsThreshold = rtsThreshold;
    wld_rad_doSync(pRad);

    SAH_TRACEZ_OUT(ME);
}

/* Setting Long Retry Limit */
static void s_setLongRetryLimit_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    uint8_t lRetryLimitValue = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set LongRetryLimit %d", pR->Name, lRetryLimitValue);
    pR->longRetryLimit = lRetryLimitValue;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setBeaconPeriod_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pR, , ME, "NULL");
    uint32_t newVal = amxc_var_dyncast(uint32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set BeaconPeriod %u", pR->Name, newVal);
    pR->beaconPeriod = newVal;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setDtimPeriod_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pR, , ME, "NULL");
    uint32_t newVal = amxc_var_dyncast(uint32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set DtimPeriod %u", pR->Name, newVal);
    pR->dtimPeriod = newVal;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

amxd_status_t _wld_rad_validatePreambleType_pvf(amxd_object_t* object,
                                                amxd_param_t* param _UNUSED,
                                                amxd_action_t reason _UNUSED,
                                                const amxc_var_t* const args,
                                                amxc_var_t* const retval _UNUSED,
                                                void* priv _UNUSED) {
    SAH_TRACEZ_IN(ME);

    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    ASSERTI_FALSE(wld_rad_is_24ghz(pRad), amxd_status_ok, ME, "2.4GHz radio");
    amxd_status_t status = amxd_status_invalid_value;
    char* newValue = amxc_var_dyncast(cstring_t, args);
    ASSERT_NOT_NULL(newValue, status, ME, "NULL");
    if(!swl_str_matches(newValue, "short")) {
        status = amxd_status_ok;
    }
    free(newValue);

    SAH_TRACEZ_OUT(ME);
    return status;
}

static void s_setPreambleType_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, , ME, "NULL");
    const char* preambleType = amxc_var_constcast(cstring_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set Preamble Type %s", pRad->Name, preambleType);
    pRad->preambleType = swl_conv_charToEnum(preambleType, g_wld_preambleType, PREAMBLE_TYPE_MAX, PREAMBLE_TYPE_AUTO);
    wld_rad_doSync(pRad);

    SAH_TRACEZ_OUT(ME);
}

static void s_setPacketAggregationEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, , ME, "NULL");
    bool packetAggregationEnable = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set packet aggregation enable %u", pRad->Name, packetAggregationEnable);
    pRad->packetAggregationEnable = packetAggregationEnable;
    wld_rad_doSync(pRad);

    SAH_TRACEZ_OUT(ME);
}

/* Setting Target Wake Time Enable */
static void s_setTargetWakeTimeEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    pR->twtEnable = amxc_var_dyncast(bool, newValue);
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setOfdmaEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    pRad->ofdmaEnable = amxc_var_dyncast(bool, newValue);
    wld_rad_doSync(pRad);

    SAH_TRACEZ_OUT(ME);
}

static void s_setHeCaps_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    const char* caps = amxc_var_constcast(cstring_t, newValue);
    wld_he_cap_m newCapsEna = swl_conv_charToMask(caps, g_str_wld_he_cap, HE_CAP_MAX);
    ASSERTS_NOT_EQUALS(newCapsEna, pRad->heCapsEnabled, , ME, "EQUAL");
    pRad->heCapsEnabled = newCapsEna;
    wld_rad_doSync(pRad);

    SAH_TRACEZ_OUT(ME);
}

static void s_set80211hEnable_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool flag = amxc_var_dyncast(bool, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set 80211hEnable %d", pR->Name, flag);
    bool enable = pR->IEEE80211hSupported && flag;
    ASSERTS_NOT_EQUALS(enable, pR->setRadio80211hEnable, , ME, "EQUAL");
    pR->setRadio80211hEnable = enable;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

amxd_status_t _wld_rad_validateIEEE80211hEnabled_pvf(amxd_object_t* object _UNUSED,
                                                     amxd_param_t* param _UNUSED,
                                                     amxd_action_t reason _UNUSED,
                                                     const amxc_var_t* const args,
                                                     amxc_var_t* const retval _UNUSED,
                                                     void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    bool flag = amxc_var_dyncast(bool, args);
    if((pRad->IEEE80211hSupported && flag) == flag) {
        return amxd_status_ok;
    }
    SAH_TRACEZ_ERROR(ME, "%s: invalid IEEE80211hEnabled %d", pRad->Name, flag);
    return amxd_status_invalid_value;
}

static swl_rc_ne s_checkCountryCode(const char* countryCode, int32_t* pIdx) {
    if(pIdx != NULL) {
        *pIdx = -1;
    }
    ASSERT_FALSE(swl_str_isEmpty(countryCode), SWL_RC_INVALID_PARAM, ME, "Empty country");
    ASSERT_FALSE(swl_str_matches(countryCode, " "), SWL_RC_ERROR, ME, "invalid country(%s)", countryCode);
    int indexCountryCode = atoi(countryCode);
    int idx = 0;
    int rc = -1;
    if(!indexCountryCode) {
        char subCC[3];
        swl_str_copy(subCC, sizeof(subCC), countryCode);
        rc = getCountryParam(subCC, 0, &idx);
    } else {
        rc = getCountryParam(NULL, indexCountryCode, &idx);
    }
    if(pIdx != NULL) {
        *pIdx = idx;
    }
    ASSERT_FALSE(rc < 0, SWL_RC_ERROR, ME, "invalid country (%s)", countryCode);
    return SWL_RC_OK;
}

static swl_rc_ne s_setCountryCode(T_Radio* pR, const char* countryCode, bool direct) {
    ASSERT_NOT_NULL(pR, SWL_RC_INVALID_PARAM, ME, "NULL");
    int idx = 0;
    swl_rc_ne rc = s_checkCountryCode(countryCode, &idx);
    ASSERT_EQUALS(rc, SWL_RC_OK, rc, ME, "%s: invalid country (%s)", pR->Name, countryCode);
    pR->regulatoryDomainIdx = idx;
    if(direct && (pR->pFA->mfn_wrad_regdomain(pR, NULL, 0, SET | DIRECT) == SWL_RC_OK)) {
        pR->pFA->mfn_wrad_poschans(pR, NULL, 0);
        return SWL_RC_OK;
    }
    pR->pFA->mfn_wrad_regdomain(pR, NULL, 0, SET);
    return rc;
}

static void s_setCountryCode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    const char* CC = amxc_var_constcast(cstring_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set CountryCode (%s)", pR->Name, CC);
    //apply immediately the country code if possible (boot case)
    //to have up to date possible channels asap
    bool initOngoing = (!wld_rad_areAllVapsDone(pR));
    if(s_setCountryCode(pR, CC, initOngoing) == SWL_RC_OK) {
        wld_autoCommitMgr_notifyRadEdit(pR);
    }

    SAH_TRACEZ_OUT(ME);
}

amxd_status_t _wld_rad_validateImplicitBeamForming_pvf(amxd_object_t* object _UNUSED,
                                                       amxd_param_t* param _UNUSED,
                                                       amxd_action_t reason _UNUSED,
                                                       const amxc_var_t* const args,
                                                       amxc_var_t* const retval _UNUSED,
                                                       void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    amxd_status_t status = amxd_status_invalid_value;
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    bool enable = amxc_var_dyncast(bool, args);

    /* Disabling is always allowed */
    if((!enable) || (pRad->implicitBeamFormingSupported)) {
        return amxd_status_ok;
    }
    SAH_TRACEZ_ERROR(ME, "%s: unsupported feature", pRad->Name);
    return status;
}

static void s_setBeamForming(T_Radio* pR, const amxc_var_t* const newValue, beamforming_type_t type) {
    SAH_TRACEZ_IN(ME);

    ASSERTI_NOT_NULL(pR, , ME, "NULL");
    bool enabled = amxc_var_dyncast(bool, newValue);
    if(type == beamforming_implicit) {
        pR->implicitBeamFormingEnabled = enabled;
    } else {
        pR->explicitBeamFormingEnabled = enabled;
    }
    int ret = pR->pFA->mfn_wrad_beamforming(pR, type, enabled, SET);
    ASSERT_EQUALS(ret, 0, , ME, "Failed to set %s beam forming",
                  type == beamforming_implicit ? "implicit" : "explicit");
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setImplicitBeamForming_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    T_Radio* pR = wld_rad_fromObj(object);
    s_setBeamForming(pR, newValue, beamforming_implicit);
}

amxd_status_t _wld_rad_validateExplicitBeamForming_pvf(amxd_object_t* object _UNUSED,
                                                       amxd_param_t* param _UNUSED,
                                                       amxd_action_t reason _UNUSED,
                                                       const amxc_var_t* const args,
                                                       amxc_var_t* const retval _UNUSED,
                                                       void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    amxd_status_t status = amxd_status_invalid_value;
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    bool enable = amxc_var_dyncast(bool, args);

    /* Disabling is always allowed */
    if((!enable) || (pRad->explicitBeamFormingSupported)) {
        return amxd_status_ok;
    }
    SAH_TRACEZ_ERROR(ME, "%s: unsupported feature", pRad->Name);
    return status;
}

static void s_setExplicitBeamForming_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    T_Radio* pR = wld_rad_fromObj(object);
    s_setBeamForming(pR, newValue, beamforming_explicit);
}

static void s_setBfCap(T_Radio* pR, const amxc_var_t* const newValue, com_dir_e comDir) {
    SAH_TRACEZ_IN(ME);

    ASSERTI_NOT_NULL(pR, , ME, "NULL");
    const char* bfStr = amxc_var_constcast(cstring_t, newValue);
    wld_rad_bf_cap_m capEnable = swl_conv_charToMask(bfStr, g_str_wld_rad_bf_cap, RAD_BF_CAP_MAX);

    ASSERTI_NOT_EQUALS(capEnable, pR->bfCapsEnabled[comDir], , ME, "%s bfCap no change %u %u",
                       pR->Name, comDir, capEnable);

    if((capEnable & !pR->bfCapsSupported[comDir]) && (capEnable != M_RAD_BF_CAP_DEFAULT)) {
        SAH_TRACEZ_WARNING(ME, "%s : enabling unsupported bf cap %u: %u sup %u", pR->Name,
                           comDir, capEnable, pR->bfCapsSupported[comDir]);
    }

    SAH_TRACEZ_INFO(ME, "%s : set bf cap %u : %u from %u / %u",
                    pR->Name, comDir, capEnable, pR->bfCapsEnabled[comDir], pR->bfCapsSupported[comDir]);

    pR->bfCapsEnabled[comDir] = capEnable;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setRxBfCapsEnabled_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    T_Radio* pR = wld_rad_fromObj(object);
    s_setBfCap(pR, newValue, COM_DIR_RECEIVE);
}

static void s_setTxBfCapsEnabled_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    T_Radio* pR = wld_rad_fromObj(object);
    s_setBfCap(pR, newValue, COM_DIR_TRANSMIT);
}

static void s_setRIFS_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    const char* rifsStr = amxc_var_constcast(cstring_t, newValue);
    pR->RIFSEnabled = swl_conv_charToEnum(rifsStr, Rad_RIFS_MODE, RIFS_MODE_MAX, RIFS_MODE_DEFAULT);
    int ret = pR->pFA->mfn_wrad_rifs(pR, pR->RIFSEnabled, SET);
    ASSERTI_EQUALS(ret, 0, , ME, "%s :Failed to put RIFS to %s", pR->Name, Rad_RIFS_MODE[pR->RIFSEnabled]);
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setAirTimeFairness_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool enabled = amxc_var_dyncast(bool, newValue);
    pR->airtimeFairnessEnabled = enabled;
    int ret = pR->pFA->mfn_wrad_airtimefairness(pR, enabled, SET);
    ASSERTI_EQUALS(ret, 0, , ME, "%s: Failed to %s AirTimeFairness", pR->Name, enabled ? "enable" : "disable");
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

/**
 * Callback function to set intelligent airtime.
 * This function will call the driver set intelligent airtime with the updated enable flag.
 * It will only do so if the current intelligentAirtimeScheduleEnabled status differs from the requested
 * state.
 * Note! It is up to the plugin to actually change the radio value, if valid.
 */
static void s_setIntelligentAirtime_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool enabled = amxc_var_dyncast(bool, newValue);
    ASSERTI_NOT_EQUALS(enabled, pR->intAirtimeSchedEnabled, , ME, "%s: keep %u", pR->Name, enabled);
    int ret = pR->pFA->mfn_wrad_intelligentAirtime(pR, enabled, SET);
    ASSERTI_EQUALS(ret, 0, , ME, "%s: fail %u", pR->Name, enabled);
    SAH_TRACEZ_INFO(ME, "%s: set %u", pR->Name, enabled);
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setRxPowerSave_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    bool enabled = amxc_var_dyncast(bool, newValue);
    pR->rxPowerSaveEnabled = enabled;

    int ret = pR->pFA->mfn_wrad_rx_powersave(pR, enabled, SET);
    ASSERTI_EQUALS(ret, 0, , ME, "%s : Failed to %s power save", pR->Name, enabled ? "enable" : "disable");
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setRxPowerSaveRepeater_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    bool enabled = amxc_var_dyncast(bool, newValue);
    pR->rxPowerSaveEnabledWhenRepeater = enabled;
    if(!wld_rad_hasEnabledEp(pR)) {
        SAH_TRACEZ_INFO(ME, "%s: no active repeater, ignore set", pR->Name);
        return;
    }

    int ret = pR->pFA->mfn_wrad_rx_powersave(pR, enabled, SET);
    ASSERTI_EQUALS(ret, 0, , ME, "%s : Failed to %s power save on repeater", pR->Name, enabled ? "enable" : "disable");
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setMultiUserMIMO_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    bool enabled = amxc_var_dyncast(bool, newValue);
    pR->multiUserMIMOEnabled = enabled;

    int ret = pR->pFA->mfn_wrad_multiusermimo(pR, enabled, SET);
    ASSERTI_EQUALS(ret, 0, , ME, "%s : Failed to %s Multi-User MIMO", pR->Name, enabled ? "enable" : "disable");
    wld_autoCommitMgr_notifyRadEdit(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_saveMaxStations(T_Radio* pR) {
    ASSERT_TRUE(swl_typeUInt32_commitObjectParam(pR->pBus, "MaxAssociatedDevices", pR->maxStations), ,
                ME, "%s: fail to commit maxAssocDevices (%d)", pR->Name, pR->maxStations);
}

static void s_setMaxStations_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    int nMaxSta = amxc_var_dyncast(int32_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set MaxStations %d", pR->Name, nMaxSta);
    if((nMaxSta < 0) || (nMaxSta > (int) pR->maxNrHwSta)) {
        SAH_TRACEZ_WARNING(ME, "%s: restore Max Hw Stations %d isof conf %d", pR->Name, pR->maxNrHwSta, nMaxSta);
        swla_delayExec_add((swla_delayExecFun_cbf) s_saveMaxStations, pR);
        return;
    }
    pR->maxStations = nMaxSta;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

amxd_status_t _wld_rad_validateBasicDataRates_pvf(amxd_object_t* object,
                                                  amxd_param_t* param _UNUSED,
                                                  amxd_action_t reason _UNUSED,
                                                  const amxc_var_t* const args,
                                                  amxc_var_t* const retval _UNUSED,
                                                  void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    amxd_status_t status = amxd_status_invalid_value;
    char* newValue = amxc_var_dyncast(cstring_t, args);
    ASSERT_NOT_NULL(newValue, status, ME, "NULL");

    bool entireStringUsed;
    swl_mcs_legacyIndex_m newBasicTxRates = swl_conv_charToMaskSep(newValue, swl_mcs_legacyStrList, SWL_MCS_LEGACY_LIST_SIZE, ',', &entireStringUsed);
    free(newValue);
    bool validNewBasicTxRates = ((newBasicTxRates == 0) ||
                                 (entireStringUsed && ((newBasicTxRates & pRad->supportedDataTransmitRates) == newBasicTxRates)));
    ASSERT_TRUE(validNewBasicTxRates, status, ME, "%s: One or more of the configured basic rates are not supported", pRad->Name);
    if(pRad->operationalDataTransmitRates != 0) {
        SAH_TRACE_ERROR("= newBasicTxRates = %u, operationalDataTransmitRates = %u ==> %u", newBasicTxRates, pRad->operationalDataTransmitRates, newBasicTxRates & pRad->operationalDataTransmitRates);
        ASSERT_TRUE(((newBasicTxRates & pRad->operationalDataTransmitRates) != 0), status, ME, "%s: Not allowed to disable all the basic transmit rates", pRad->Name);
    }

    return amxd_status_ok;
}

static void s_setBasicDataTransmitRates_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    char* basicDataTransmitRates = amxc_var_dyncast(cstring_t, newValue);
    pRad->basicDataTransmitRates = swl_conv_charToMask(basicDataTransmitRates, swl_mcs_legacyStrList, SWL_MCS_LEGACY_LIST_SIZE);
    free(basicDataTransmitRates);
    wld_rad_doSync(pRad);
}

amxd_status_t _wld_rad_validateOperationalDataRates_pvf(amxd_object_t* object,
                                                        amxd_param_t* param _UNUSED,
                                                        amxd_action_t reason _UNUSED,
                                                        const amxc_var_t* const args,
                                                        amxc_var_t* const retval _UNUSED,
                                                        void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTI_NOT_NULL(pRad, amxd_status_ok, ME, "No radio mapped");
    amxd_status_t status = amxd_status_invalid_value;
    char* newValue = amxc_var_dyncast(cstring_t, args);
    ASSERT_NOT_NULL(newValue, status, ME, "NULL");

    bool entireStringUsed;
    swl_mcs_legacyIndex_m newOperationalTxRates = swl_conv_charToMaskSep(newValue, swl_mcs_legacyStrList, SWL_MCS_LEGACY_LIST_SIZE, ',', &entireStringUsed);
    free(newValue);

    if((newOperationalTxRates == 0) || (newOperationalTxRates == pRad->supportedDataTransmitRates)) {
        status = amxd_status_ok;
    } else {
        ASSERT_TRUE(entireStringUsed && ((newOperationalTxRates & pRad->supportedDataTransmitRates) == newOperationalTxRates),
                    status, ME, "%s: One or more of the configured operational rates are not supported", pRad->Name);
        ASSERT_NOT_EQUALS(pRad->basicDataTransmitRates, 0, status, ME, "%s: BasicDataRates must be set before to prevent disabling all basic supported rates", pRad->Name);
        swl_mcs_legacyIndex_m newBasicTxRates = newOperationalTxRates & pRad->basicDataTransmitRates;
        ASSERT_NOT_EQUALS(newBasicTxRates, 0, status, ME, "%s: Not allowed to disable all the basic transmit rates", pRad->Name);
        status = amxd_status_ok;
    }
    return status;
}

static void s_setOperationalDataTransmitRates_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    char* operationalDataTransmitRates = amxc_var_dyncast(cstring_t, newValue);
    pRad->operationalDataTransmitRates = swl_conv_charToMask(operationalDataTransmitRates, swl_mcs_legacyStrList, SWL_MCS_LEGACY_LIST_SIZE);
    free(operationalDataTransmitRates);
    wld_rad_doSync(pRad);
}

amxd_status_t _wld_rad_getLastChange_prf(amxd_object_t* object,
                                         amxd_param_t* param,
                                         amxd_action_t reason,
                                         const amxc_var_t* const args _UNUSED,
                                         amxc_var_t* const retval,
                                         void* priv _UNUSED) {
    ASSERTS_NOT_NULL(param, amxd_status_unknown_error, ME, "NULL");
    ASSERTS_EQUALS(reason, action_param_read, amxd_status_function_not_implemented, ME, "not impl");
    T_Radio* pRad = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pRad, amxd_status_ok, ME, "no radio mapped");
    uint32_t lastChange = swl_time_getMonoSec() - pRad->changeInfo.lastStatusChange;
    amxc_var_set(uint32_t, retval, lastChange);
    return amxd_status_ok;
}

// See this as
const char* DFST_RP_Help[] = {
    "DFS_drvdbg (dbg_action:string) the DFS Radar pulse command",
    "-h shows full help",
    NULL
};

const char* DFST_RP_STR[] = {
    "Do nothing (default)",                                                  /* DFST_RP_NONE */
    "DFS Radar Pulse when ON-channel",                                       /* DFST_RP_ON_CH      */
    "DFS Radar Pulse when OFF-Channel",                                      /* DFST_RP_OFF_CH     */
    "Driver default Radar Pulse (When on DFS \"ON\", when on !DFS \"OFF\")", /* DFST_RP_AUTO       */
    "Force to Clear Radar status",                                           /* DFST_RP_CLR_STATUS */
    "Force to Clear Out of Service channels",                                /* DFST_RP_CLR_OSC    */
    NULL                                                                     /* DFST_RP_MAX */
};

/**
 * @name setDFSForceRadarTrigger
 * @details This function is a helper for DFS AUTOMATION testing actions.
 *
 */
amxd_status_t _DFS_drvdbg(amxd_object_t* object,
                          amxd_function_t* func _UNUSED,
                          amxc_var_t* args,
                          amxc_var_t* retval) {
    T_Radio* pR = NULL;
    int act_idx;

    SAH_TRACEZ_IN(ME);
    /* Check our input data */
    pR = object->priv;
    const char* StrVal = GET_CHAR(args, "dbg_action");

    // String as we can parse it in any format?
    if(!pR) {
        SAH_TRACEZ_ERROR(ME, "pR %p", pR);
        amxc_var_set(int32_t, retval, -1);
        return amxd_status_ok;
    }

    if(!(StrVal && StrVal[0])) {
        SAH_TRACEZ_ERROR(ME, "StrVal %p", StrVal);
        amxc_var_set(int32_t, retval, -2);
        return amxd_status_unknown_error;
    }

    /* Analyse input, do we've options? Options start with a dash '-'? */
    /* -h = help */
    if(StrVal[0] == '-') {
        switch(StrVal[1]) {
        case 'h':
        case 'H':
        default:
        {
            int i;
            SAH_TRACEZ_ERROR(ME, "Syntax Help");
            for(i = 0; DFST_RP_Help[i]; i++) {
                SAH_TRACEZ_ERROR(ME, "%s", DFST_RP_Help[i]);
            }
            // Default Index number action.
            for(i = 0; DFST_RP_STR[i]; i++) {
                SAH_TRACEZ_ERROR(ME, "%d - %s", i, DFST_RP_STR[i]);
            }
        }
            amxc_var_set(uint32_t, retval, 1);
            return amxd_status_ok;
        }
    }
    // Currently we only support/parse a number as action
    act_idx = atoi(StrVal);
    ASSERTS_TRUE((act_idx >= 0 && act_idx < DFST_RP_MAX), amxd_status_unknown_error, ME, "act_idx out of bound");

    /* Call the supported driver command */
    SAH_TRACEZ_ERROR(ME, "Req %s", DFST_RP_STR[act_idx]);
    pR->pFA->mfn_wrad_dfsradartrigger(pR, act_idx);

    /* Update ongoing channel states...*/
    wld_rad_triggerDelayCommit(pR, 1000, false);

    amxc_var_set(int32_t, retval, 0);
    return amxd_status_ok;
}

static void s_setDfsFileLogLimit_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    uint8_t newFileLogLimit = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: setFileLogLimit oldValue:%d newValue:%d", pR->Name, pR->dfsFileLogLimit, newFileLogLimit);
    pR->dfsFileLogLimit = newFileLogLimit;

    SAH_TRACEZ_OUT(ME);
}

static void s_setDfsEventLogLimit_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    uint8_t newEventLogLimit = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: setEventLogLimit oldValue:%d newValue:%d", pR->Name, pR->dfsEventLogLimit, newEventLogLimit);
    pR->dfsEventLogLimit = newEventLogLimit;
    checkRadioEventLogLimitReached(pR);

    SAH_TRACEZ_OUT(ME);
}

SWLA_DM_HDLRS(sDfsDmHdlrs,
              ARR(SWLA_DM_PARAM_HDLR("FileLogLimit", s_setDfsFileLogLimit_pwf),
                  SWLA_DM_PARAM_HDLR("EventLogLimit", s_setDfsEventLogLimit_pwf)));

void _wld_rad_setDFSConfig_ocf(const char* const sig_name,
                               const amxc_var_t* const data,
                               void* const priv) {
    swla_dm_procObjEvtOfLocalDm(&sDfsDmHdlrs, sig_name, data, priv);
}

amxd_status_t _wld_rad_validateOperatingFrequencyBand_pvf(amxd_object_t* object _UNUSED,
                                                          amxd_param_t* param,
                                                          amxd_action_t reason _UNUSED,
                                                          const amxc_var_t* const args,
                                                          amxc_var_t* const retval _UNUSED,
                                                          void* priv _UNUSED) {
    ASSERTS_FALSE(amxc_var_is_null(args), amxd_status_invalid_value, ME, "invalid");
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, amxd_status_ok, ME, "obj is not instance");
    SAH_TRACEZ_IN(ME);

    const char* oname = amxd_object_get_name(object, AMXD_OBJECT_NAMED);
    amxd_status_t status = amxd_status_invalid_value;
    const char* currentValue = amxc_var_constcast(cstring_t, &param->value);
    ASSERT_NOT_NULL(currentValue, status, ME, "NULL");
    char* newValue = amxc_var_dyncast(cstring_t, args);
    ASSERT_NOT_NULL(newValue, status, ME, "NULL");
    swl_freqBandExt_e bandExt = swl_conv_charToEnum(newValue, Rad_SupFreqBands, SWL_FREQ_BAND_EXT_MAX, SWL_FREQ_BAND_EXT_NONE);
    swl_freqBand_e band = swl_chanspec_freqBandExtToFreqBand(bandExt, SWL_FREQ_BAND_MAX, NULL);

    T_Radio* pRad = wld_rad_fromObj(object);
    if(pRad == NULL) {
        pRad = wld_getUinitRadioByBand(bandExt);
    }

    if((swl_str_matches(currentValue, newValue)) ||
       ((band != SWL_FREQ_BAND_MAX) && (pRad != NULL) && (SWL_BIT_SET(pRad->supportedFrequencyBands, bandExt)))) {
        status = amxd_status_ok;
    } else {
        SAH_TRACEZ_ERROR(ME, "%s: No available radio device supporting Frequency band -%s- -%s- %p", oname, currentValue, newValue, pRad);
        // allow unsupported freqBand on initial load, keeping radio at status "Not present"
        if(swl_str_isEmpty(currentValue)) {
            status = amxd_status_ok;
        }
    }
    free(newValue);

    SAH_TRACEZ_OUT(ME);
    return status;
}

amxd_status_t _wld_rad_setOperatingFrequencyBand_pwf(amxd_object_t* object,
                                                     amxd_param_t* parameter,
                                                     amxd_action_t reason,
                                                     const amxc_var_t* const args,
                                                     amxc_var_t* const retval,
                                                     void* priv) {
    SAH_TRACEZ_IN(ME);

    amxd_status_t rv = amxd_action_param_write(object, parameter, reason, args, retval, priv);
    ASSERT_EQUALS(rv, amxd_status_ok, rv, ME, "fail to write param rv:%d", rv);
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, rv, ME, "Not instance");

    const char* OFB = amxc_var_constcast(cstring_t, args);
    ASSERTW_STR(OFB, amxd_status_invalid_value, ME, "Missing param");

    swl_freqBandExt_e band = swl_conv_charToEnum(OFB, Rad_SupFreqBands, SWL_FREQ_BAND_EXT_MAX, SWL_FREQ_BAND_EXT_2_4GHZ);

    amxd_status_t get_status = amxd_status_ok;
    int32_t wiPhyId = amxd_object_get_int32_t(object, "WiPhyId", &get_status);
    if(get_status != amxd_status_ok) {
        SAH_TRACEZ_WARNING(ME, "can not read predef WiPhyId param");
        wiPhyId = -1;
    }
    SAH_TRACEZ_INFO(ME, "Value of WiPhyId:%d", wiPhyId);

    /* Link radio/object */
    bool newMap = false;
    if(object->priv == NULL) {
        newMap = (_linkFirstUinitRadio(object, band, wiPhyId) == amxd_status_ok);
    }

    T_Radio* pR = object->priv;
    ASSERT_NOT_NULL(pR, amxd_status_ok, ME, "No mapped radio");
    ASSERT_TRUE(debugIsRadPointer(pR), amxd_status_unknown_error, ME, "invalid radio Ctx");
    if(newMap || (pR->operatingFrequencyBand != band)) {
        pR->operatingFrequencyBand = band;
        SAH_TRACEZ_INFO(ME, "%s: set OperatingFrequencyBand %s %d", pR->Name, OFB, band);
        pR->fsmRad.FSM_SyncAll = TRUE;
        wld_rad_doCommitIfUnblocked(pR);
    }

    SAH_TRACEZ_OUT(ME);
    return amxd_status_ok;
}

static void s_syncRadDm(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");

    SAH_TRACEZ_IN(ME);

    pRad->pFA->mfn_sync_radio(pRad->pBus, pRad, SET);
    if(pRad == wld_firstRad()) {
        syncData_VendorWPS2OBJ(amxd_object_get(get_wld_object(), "wps_DefParam"), pRad, SET);
    }

    SAH_TRACEZ_OUT(ME);
}

static void s_writeWiFi7Cap(amxd_trans_t* trans, amxd_object_t* obj, wld_radioWiFi7Cap_t* cap) {
    ASSERT_NOT_NULL(obj, , ME, "NULL");
    amxd_trans_select_object(trans, obj);
    amxd_trans_set_bool(trans, "EMLMRSupport", cap->emlmrSupported);
    amxd_trans_set_bool(trans, "EMLSRSupport", cap->emlsrSupported);
    amxd_trans_set_bool(trans, "STRSupport", cap->strSupported);
    amxd_trans_set_bool(trans, "NSTRSupport", cap->nstrSupported);
}

static void s_writeCapabilities(T_Radio* pR) {

    amxd_object_t* capObj = amxd_object_findf(pR->pBus, "Capabilities");
    amxd_trans_t trans;
    ASSERT_TRANSACTION_INIT(capObj, &trans, , ME, "%s : trans init failure", pR->Name);


    s_writeWiFi7Cap(&trans, amxd_object_findf(capObj, "WiFi7APRole"), &pR->cap.apCap7);
    s_writeWiFi7Cap(&trans, amxd_object_findf(capObj, "WiFi7STARole"), &pR->cap.staCap7);

    ASSERT_TRANSACTION_LOCAL_DM_END(&trans, , ME, "%s : trans apply failure", pR->Name);
}

static void s_addInstance_oaf(void* priv _UNUSED, amxd_object_t* object, const amxc_var_t* const intialParamValues) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, , ME, "NO radio Ctx");

    const char* OFB = GET_CHAR(intialParamValues, "OperatingFrequencyBand");
    SAH_TRACEZ_INFO(ME, "%s: added instance object(%p:%s:%s)", pR->Name, object, pR->instanceName, OFB);
    wld_rad_init_counters(pR, &pR->genericCounters, radCounterDefaults);
    wld_radStaMon_init(pR);
    s_writeCapabilities(pR);
    syncData_VendorWPS2OBJ(NULL, pR, GET);
    //event instance-added received, radio object is writable (loading transaction is done)
    pR->hasDmReady = true;
    wld_rad_triggerChangeEvent(pR, WLD_RAD_CHANGE_INIT, NULL);

    //delay sync Rad Dm after all conf has been loaded
    swla_delayExec_add((swla_delayExecFun_cbf) s_syncRadDm, pR);

    SAH_TRACEZ_OUT(ME);
}

/**************************************************************************************************/
/**************************************************************************************************/
T_Radio* wld_getRadioDataHandler(amxd_object_t* pobj, const char* rn) {
    ASSERTS_NOT_NULL(pobj, NULL, ME, "NULL");
    amxd_object_t* radio = amxd_object_get_child(pobj, "Radio");
    amxd_object_t* inst = amxd_object_get_instance(radio, rn, 0);
    ASSERTI_NOT_NULL(inst, NULL, ME, "%s - %p\n", rn, inst);
    return (T_Radio*) inst->priv;
}

void wld_radio_updateAntennaExt(T_Radio* pRad, amxd_trans_t* trans) {
    amxd_object_t* hwObject = amxd_object_findf(pRad->pBus, "DriverStatus");

    swla_trans_t tmpTrans;
    amxd_trans_t* targetTrans = swla_trans_init(&tmpTrans, trans, hwObject);
    ASSERT_NOT_NULL(targetTrans, , ME, "NULL");

    amxd_trans_set_int32_t(targetTrans, "NrTxAntenna", pRad->nrAntenna[COM_DIR_TRANSMIT]);
    amxd_trans_set_int32_t(targetTrans, "NrRxAntenna", pRad->nrAntenna[COM_DIR_RECEIVE]);
    amxd_trans_set_int32_t(targetTrans, "NrActiveTxAntenna", pRad->nrActiveAntenna[COM_DIR_TRANSMIT]);
    amxd_trans_set_int32_t(targetTrans, "NrActiveRxAntenna", pRad->nrActiveAntenna[COM_DIR_RECEIVE]);

    swla_trans_finalize(&tmpTrans, NULL);
}

void wld_radio_updateAntenna(T_Radio* pRad) {
    wld_radio_updateAntennaExt(pRad, NULL);
}

/**
 * Return appropriate discovery method for one radio
 *
 * If all APs are disabled, return DISABLED
 *
 * If any AP is configured as NON default, it should configured
 * the non default value of first AP
 *
 * If all APs are configured as default then:
 * - If 2.4 / 5GHz it returns RNR
 * - If 6GHz, if any AP is not annouced in RNR on 2.4 or 5, return
 *   UPR otherwise DISABLED
 */
wld_ap_dm_m wld_rad_getDiscoveryMethod(T_Radio* pR) {
    ASSERTI_NOT_NULL(pR, M_AP_DM_DEFAULT, ME, "NULL");
    wld_ap_dm_m dm = M_AP_DM_DISABLED;

    T_AccessPoint* pAP = NULL;
    wld_rad_forEachAp(pAP, pR) {
        if(!pAP->enable) {
            continue;
        }
        if(pAP->discoveryMethod != M_AP_DM_DEFAULT) {
            return pAP->discoveryMethod;
        }

        dm = wld_ap_getDiscoveryMethod(pAP);
        /* If at least on 6GHz BSS needs UPR, apply it */
        if((pR->operatingFrequencyBand == SWL_FREQ_BAND_EXT_6GHZ) &&
           (dm == M_AP_DM_UPR)) {
            break;
        }
    }
    char dmStr[64] = {0};
    swl_conv_maskToCharSep(dmStr, sizeof(dmStr), dm, g_str_wld_ap_dm, AP_DM_MAX, ',');
    SAH_TRACEZ_INFO(ME, "%s: Apply DM <%s>", pR->Name, dmStr);
    return dm;
}

void wld_rad_updateDiscoveryMethod6GHz() {
    T_Radio* pR = wld_getRadioByFrequency(SWL_FREQ_BAND_6GHZ);
    ASSERTI_NOT_NULL(pR, , ME, "NULL");
    SAH_TRACEZ_INFO(ME, "%s: 6G radio needs discovery method update", pR->Name);
    T_AccessPoint* pAP = NULL;
    wld_rad_forEachAp(pAP, pR) {
        pAP->pFA->mfn_wvap_set_discovery_method(pAP);
    }
}

static void s_listRadioFeatures(T_Radio* pRad, amxc_var_t* map) {
    for(int i = 0; i < WLD_FEATURE_MAX; i++) {
        amxc_var_add_key(uint32_t, map, g_wld_radFeatures_str[i], pRad->features[i]);
    }
}

void syncData_Radio2OBJ(amxd_object_t* object, T_Radio* pR, int set) {
    char ValBuf[32];
    char TBuf[320];
    char objPath[128];

    SAH_TRACEZ_IN(ME);

    if(!(object && pR)) {
        /* Missing data pointers... */
        return;
    }

    if(set & SET) {
        memset(ValBuf, 0, sizeof(ValBuf));
        memset(TBuf, 0, sizeof(TBuf));
        memset(objPath, 0, sizeof(objPath));
        /* Set Radio data in mapped OBJ structure */
        /* update saved radio base mac address: matching primary interface (first endpoint/vap) */
        char macStr[SWL_MAC_CHAR_LEN] = {0};
        SWL_MAC_BIN_TO_CHAR(macStr, pR->MACAddr);

        amxd_trans_t trans;
        ASSERT_TRANSACTION_INIT(object, &trans, , ME, "%s : trans init failure", pR->Name);


        amxd_trans_set_cstring_t(&trans, "VendorPCISig", pR->vendor->name);
        amxd_trans_set_cstring_t(&trans, "Name", pR->Name);

        wld_util_initCustomAlias(&trans, object);

        amxd_trans_set_cstring_t(&trans, "BaseMACAddress", macStr);
        amxd_trans_set_cstring_t(&trans, "Status", Rad_SupStatus[pR->status]);
        amxd_trans_set_int32_t(&trans, "Index", pR->index);

        amxd_trans_set_int32_t(&trans, "MaxBitRate", pR->maxBitRate);

        swl_conv_transParamSetMask(&trans, "SupportedFrequencyBands", pR->supportedFrequencyBands, swl_freqBandExt_str, SWL_FREQ_BAND_MAX);

        amxd_trans_set_cstring_t(&trans, "OperatingFrequencyBand", Rad_SupFreqBands[pR->operatingFrequencyBand]);

        swl_radStd_supportedStandardsToChar(TBuf, sizeof(TBuf), pR->supportedStandards, pR->operatingStandardsFormat);
        amxd_trans_set_cstring_t(&trans, "SupportedStandards", TBuf);

        swl_conv_transParamSetMask(&trans, "HeCapsSupported", pR->heCapsSupported, g_str_wld_he_cap, HE_CAP_MAX);

        swl_conv_transParamSetMask(&trans, "TxBeamformingCapsAvailable", pR->bfCapsSupported[COM_DIR_TRANSMIT], g_str_wld_rad_bf_cap, RAD_BF_CAP_MAX);

        swl_conv_transParamSetMask(&trans, "RxBeamformingCapsAvailable", pR->bfCapsSupported[COM_DIR_RECEIVE], g_str_wld_rad_bf_cap, RAD_BF_CAP_MAX);

        ssize_t outputSize = 0;
        //HT capabilities
        char capBuffer[256] = {0};
        outputSize = swl_base64_encode(capBuffer, sizeof(capBuffer), (swl_bit8_t*) &pR->htCapabilities, sizeof(pR->htCapabilities));
        if(outputSize >= (int) sizeof(capBuffer)) {
            SAH_TRACEZ_WARNING(ME, "too small buffer %zi, Needed size is %zd", sizeof(capBuffer), outputSize);
        } else {
            amxd_trans_set_cstring_t(&trans, "HTCapabilities", capBuffer);
        }
        outputSize = swl_80211_htCapMaskToChar(capBuffer, sizeof(capBuffer), pR->htCapabilities);
        if(outputSize >= (int) sizeof(capBuffer)) {
            SAH_TRACEZ_WARNING(ME, "too small buffer %zi, Needed size is %zd", sizeof(capBuffer), outputSize);
        }
        amxd_trans_set_cstring_t(&trans, "RadCapabilitiesHTStr", capBuffer);

        //VHT capabilities
        outputSize = swl_base64_encode(capBuffer, sizeof(capBuffer), (swl_bit8_t*) &pR->vhtCapabilities, sizeof(pR->vhtCapabilities));
        if(outputSize >= (int) sizeof(capBuffer)) {
            SAH_TRACEZ_WARNING(ME, "too small buffer %zi, Needed size is %zd", sizeof(capBuffer), outputSize);
        } else {
            amxd_trans_set_cstring_t(&trans, "VHTCapabilities", capBuffer);
        }
        outputSize = swl_80211_vhtCapMaskToChar(capBuffer, sizeof(capBuffer), pR->vhtCapabilities);
        if(outputSize >= (int) sizeof(capBuffer)) {
            SAH_TRACEZ_WARNING(ME, "too small buffer %zi, Needed size is %zd", sizeof(capBuffer), outputSize);
        }
        amxd_trans_set_cstring_t(&trans, "RadCapabilitiesVHTStr", capBuffer);

        //HE Physical Capabilities
        outputSize = swl_base64_encode(capBuffer, sizeof(capBuffer), (swl_bit8_t*) &pR->hePhyCapabilities, sizeof(pR->hePhyCapabilities));
        if(outputSize >= (int) sizeof(capBuffer)) {
            SAH_TRACEZ_WARNING(ME, "too small buffer %zi, Needed size is %zd", sizeof(capBuffer), outputSize);
        } else {
            amxd_trans_set_cstring_t(&trans, "HECapabilities", capBuffer);
        }
        // pR->hePhyCapabilities.cap is 10 bytes, the last 2 bytes are empty, thus we can just read the first 8 bytes
        swl_80211_hePhyCapInfo_m heCap = 0;
        memcpy(&heCap, pR->hePhyCapabilities.cap, sizeof(swl_80211_hePhyCapInfo_m));
        outputSize = swl_80211_hePhyCapMaskToChar(capBuffer, sizeof(capBuffer), heCap);
        if(outputSize >= (int) sizeof(capBuffer)) {
            SAH_TRACEZ_WARNING(ME, "too small buffer %zi, Needed size is %zd", sizeof(capBuffer), outputSize);
        }
        amxd_trans_set_cstring_t(&trans, "RadCapabilitiesHePhysStr", capBuffer);


        amxd_trans_set_cstring_t(&trans, "OperatingStandardsFormat", swl_radStd_formatToChar(pR->operatingStandardsFormat));

        swl_radStd_toChar(TBuf, sizeof(TBuf), pR->operatingStandards, pR->operatingStandardsFormat, pR->supportedStandards);
        amxd_trans_set_cstring_t(&trans, "OperatingStandards", TBuf);

        swl_conv_uint8ArrayToChar(TBuf, sizeof(TBuf), pR->possibleChannels, pR->nrPossibleChannels);
        amxd_trans_set_cstring_t(&trans, "PossibleChannels", TBuf);
        swl_conv_transParamSetMask(&trans, "SupportedOperatingChannelBandwidth", pR->supportedChannelBandwidth, swl_radBw_str, SWL_RAD_BW_MAX);
        amxd_trans_set_int32_t(&trans, "AutoChannelSupported", pR->autoChannelSupported);
        amxd_trans_set_int32_t(&trans, "AutoChannelRefreshPeriod", pR->autoChannelRefreshPeriod);
        amxd_trans_set_cstring_t(&trans, "MaxChannelBandwidth", Rad_SupBW[pR->maxChannelBandwidth]);
        amxd_trans_set_cstring_t(&trans, "AutoBandwidthSelectMode", wld_rad_autoBwSelectMode_str[pR->autoBwSelectMode]);
        amxd_trans_set_bool(&trans, "ObssCoexistenceEnable", pR->obssCoexistenceEnabled);
        amxd_trans_set_cstring_t(&trans, "ExtensionChannel", Rad_SupCExt[pR->extensionChannel]);
        if(pR->channel) {
            wld_chanmgt_saveChanges(pR, &trans);
            wld_rad_updateOperatingClass(pR);
        }

        if(amxd_object_get_uint32_t(object, "MaxAssociatedDevices", NULL) == 0) {
            amxd_trans_set_uint32_t(&trans, "MaxAssociatedDevices", pR->maxStations);
        }
        amxd_trans_set_uint32_t(&trans, "MaxSupportedSSIDs", pR->maxNrHwBss);
        amxd_trans_set_cstring_t(&trans, "GuardInterval", Rad_SupGI[pR->guardInterval]);
        amxd_trans_set_int32_t(&trans, "MCS", pR->MCS);
        TBuf[0] = '\0';
        for(uint32_t idx = 0; !idx || pR->transmitPowerSupported[idx]; idx++) {
            swl_strlst_catFormat(TBuf, sizeof(TBuf), ",", "%d", pR->transmitPowerSupported[idx]);
        }
        amxd_trans_set_cstring_t(&trans, "TransmitPowerSupported", TBuf);
        amxd_trans_set_uint8_t(&trans, "RetryLimit", pR->retryLimit);
        amxd_trans_set_uint8_t(&trans, "LongRetryLimit", pR->longRetryLimit);
        amxd_trans_set_bool(&trans, "IEEE80211hSupported", pR->IEEE80211hSupported);
        amxd_trans_set_bool(&trans, "IEEE80211hEnabled", pR->setRadio80211hEnable);
        amxd_trans_set_bool(&trans, "IEEE80211kSupported", pR->IEEE80211kSupported);
        amxd_trans_set_bool(&trans, "IEEE80211rSupported", pR->IEEE80211rSupported);
        swl_conv_transParamSetMask(&trans, "MultiAPTypesSupported", pR->m_multiAPTypesSupported, cstr_MultiAPType, MULTIAP_MAX);




        TBuf[0] = '\0';
        pR->pFA->mfn_wrad_regdomain(pR, TBuf, sizeof(TBuf), GET);
        amxd_trans_set_cstring_t(&trans, "RegulatoryDomain", TBuf);

        amxd_trans_set_bool(&trans, "MultiUserMIMOSupported", pR->multiUserMIMOSupported);
        amxd_trans_set_bool(&trans, "ImplicitBeamFormingSupported", pR->implicitBeamFormingSupported);
        amxd_trans_set_bool(&trans, "ImplicitBeamFormingEnabled", pR->implicitBeamFormingEnabled);
        amxd_trans_set_bool(&trans, "ExplicitBeamFormingSupported", pR->explicitBeamFormingSupported);
        amxd_trans_set_bool(&trans, "ExplicitBeamFormingEnabled", pR->explicitBeamFormingEnabled);
        amxd_trans_set_cstring_t(&trans, "RIFSEnabled", Rad_RIFS_MODE[pR->RIFSEnabled]);
        amxd_trans_set_bool(&trans, "AirtimeFairnessEnabled", pR->airtimeFairnessEnabled);
        amxd_trans_set_uint32_t(&trans, "DFSChannelChangeEventCounter", pR->DFSChannelChangeEventCounter);
        swl_type_toTransParam(&gtSwl_type_timeReal, &trans, "DFSChannelChangeEventTimestamp", &pR->DFSChannelChangeEventTimestamp);


        /* 'IEEE80211_Caps' S@H, shows the supported driver capabilities.
           When a VAP is attached, this field is update so don't fake it!
         */
        TBuf[0] = '\0';
        pR->pFA->mfn_misc_has_support(pR, NULL, TBuf, sizeof(TBuf)); /* Get fake value we need a VAP */
        amxd_trans_set_cstring_t(&trans, "IEEE80211_Caps", TBuf);

        amxd_trans_set_cstring_t(&trans, "FirmwareVersion", pR->firmwareVersion);

        TBuf[0] = '\0';
        swl_conv_maskToCharSep(TBuf, sizeof(TBuf), pR->supportedDataTransmitRates, swl_mcs_legacyStrList, SWL_MCS_LEGACY_LIST_SIZE, ',');
        amxd_trans_set_cstring_t(&trans, "SupportedDataTransmitRates", TBuf);

        wld_rad_update_operating_standard(pR, &trans);

        wld_rad_updateCapabilities(pR, &trans);
        wld_radio_updateAntennaExt(pR, &trans);
        wld_radio_updateNaStaMonitor(pR, &trans);
        wld_bgdfs_update(pR, &trans);

        ASSERT_TRANSACTION_LOCAL_DM_END(&trans, , ME, "%s : trans apply failure", pR->Name);
        // When transaction successfully ends, it means DM is ready.
        pR->hasDmReady = true;
    } else {
        int32_t tmp_int32 = 0;
        bool tmp_bool = false;
        bool commit = false;

        tmp_bool = amxd_object_get_bool(object, "AutoChannelEnable", NULL);
        tmp_bool &= (!wld_rad_hasRunningEndpoint(pR));
        if(!pR->externalAcsMgmt && (pR->autoChannelEnable != tmp_bool)) {
            pR->autoChannelEnable = tmp_bool;
            pR->pFA->mfn_wrad_autochannelenable(pR, pR->autoChannelEnable, SET);
        }

        tmp_bool = amxd_object_get_bool(object, "TargetWakeTimeEnable", NULL);
        if(pR->twtEnable != tmp_bool) {
            pR->twtEnable = tmp_bool;
            pR->pFA->mfn_wrad_sync(pR, SET);
            commit = true;
        }

        char* operatingStandardsFormatStr = amxd_object_get_cstring_t(object, "OperatingStandardsFormat", NULL);
        pR->operatingStandardsFormat = swl_radStd_charToFormat(operatingStandardsFormatStr);
        free(operatingStandardsFormatStr);
        operatingStandardsFormatStr = NULL;

        char* newStandardStr = amxd_object_get_cstring_t(object, "OperatingStandards", NULL);
        swl_radioStandard_m newStandard = 0;
        swl_radStd_fromCharAndValidate(&newStandard, newStandardStr, pR->operatingStandardsFormat, pR->supportedStandards, "syncData_Radio2OBJ");
        if(pR->operatingStandards != newStandard) {
            pR->pFA->mfn_wrad_supstd(pR, newStandard);
            commit = true;
        }
        free(newStandardStr);
        newStandardStr = NULL;

        tmp_bool = amxd_object_get_bool(object, "OfdmaEnable", NULL);
        if(pR->ofdmaEnable != tmp_bool) {
            pR->ofdmaEnable = tmp_bool;
            pR->pFA->mfn_wrad_sync(pR, SET);
            commit = true;
        }

        tmp_bool = amxd_object_get_bool(object, "RxPowerSaveEnabled", NULL);
        if(pR->rxPowerSaveEnabled != tmp_bool) {
            pR->rxPowerSaveEnabled = tmp_bool;
            pR->pFA->mfn_wrad_rx_powersave(pR, pR->rxPowerSaveEnabled, SET);
            commit = true;
        }

        tmp_bool = amxd_object_get_bool(object, "MultiUserMIMOEnabled", NULL);
        if(pR->multiUserMIMOEnabled != tmp_bool) {
            pR->multiUserMIMOEnabled = tmp_bool;
            pR->pFA->mfn_wrad_multiusermimo(pR, pR->multiUserMIMOEnabled, SET);
            commit = true;
        }

        swl_radBw_e radBw = swl_conv_objectParamEnum(object, "OperatingChannelBandwidth", swl_radBw_str, SWL_RAD_BW_MAX, SWL_RAD_BW_AUTO);
        tmp_int32 = amxd_object_get_int32_t(object, "Channel", NULL);
        if(((pR->channel != tmp_int32) || (pR->operatingChannelBandwidth != radBw)) &&
           ((pR->channelShowing == CHANNEL_INTERNAL_STATUS_SYNC) || (pR->channelShowing == CHANNEL_INTERNAL_STATUS_TARGET))) {
            pR->operatingChannelBandwidth = radBw;
            if(!pR->autoChannelEnable) {
                swl_chanspec_t chanspec = swl_chanspec_fromDm(tmp_int32, pR->operatingChannelBandwidth, pR->operatingFrequencyBand);
                wld_chanmgt_setTargetChanspec(pR, chanspec, false, CHAN_REASON_MANUAL, NULL);
                wld_autoCommitMgr_notifyRadEdit(pR);
            } else {
                pR->channelChangeReason = CHAN_REASON_AUTO;
            }
            pR->channel = tmp_int32;
        }

        tmp_int32 = amxd_object_get_int32_t(object, "AutoChannelRefreshPeriod", NULL);
        if(pR->autoChannelRefreshPeriod != tmp_int32) {
            pR->autoChannelRefreshPeriod = tmp_int32;
            pR->pFA->mfn_wrad_achrefperiod(pR, pR->autoChannelRefreshPeriod, SET);
        }

        char* guardInt = amxd_object_get_cstring_t(object, "GuardInterval", NULL);
        if(!swl_str_nmatches(Rad_SupGI[pR->guardInterval], guardInt, strlen(Rad_SupGI[pR->guardInterval]))) {
            pR->guardInterval = swl_conv_charToEnum(guardInt, Rad_SupGI, SWL_ARRAY_SIZE(Rad_SupGI), SWL_SGI_AUTO);
            pR->pFA->mfn_wrad_guardintval(pR, NULL, pR->guardInterval, SET);
            commit = true;
        }
        free(guardInt);
        guardInt = NULL;

        tmp_int32 = amxd_object_get_int32_t(object, "MCS", NULL);
        if(pR->MCS != tmp_int32) {
            pR->MCS = tmp_int32;
            pR->pFA->mfn_wrad_mcs(pR, NULL, pR->MCS, SET);
        }

        //if(get_OBJ_ParameterHelper(TPH_INT32, object, "TransmitPower", &pR->transmitPower))
        //	pR->pFA->mfn_wrad_txpow(pR,pR->transmitPower,SET);

        tmp_int32 = amxd_object_get_int32_t(object, "MaxAssociatedDevices", NULL);
        if((tmp_int32 > 0) && (pR->maxStations != tmp_int32)) {
            pR->maxStations = tmp_int32;
            pR->pFA->mfn_wrad_sync(pR, SET);
            commit = true;
        }

        tmp_bool = amxd_object_get_bool(object, "IEEE80211hEnabled", NULL);
        if(pR->setRadio80211hEnable != tmp_bool) {
            pR->setRadio80211hEnable = tmp_bool;
            pR->pFA->mfn_wrad_sync(pR, SET);
            commit = true;
        }
        /* Get AP data from OBJ to Radio */
        tmp_int32 = amxd_object_get_bool(object, "Enable", NULL);
        if(pR->enable != tmp_int32) {
            pR->enable = tmp_int32;
            pR->pFA->mfn_wrad_enable(pR, pR->enable, SET);
            commit = true;
        }

        char* regulatoryDomain = amxd_object_get_cstring_t(object, "RegulatoryDomain", NULL);
        if(!swl_str_matches(regulatoryDomain, pR->regulatoryDomain)) {
            if(s_setCountryCode(pR, regulatoryDomain, false) == SWL_RC_OK) {
                commit = true;
            }
        }
        free(regulatoryDomain);
        regulatoryDomain = NULL;

        if(commit) {
            wld_autoCommitMgr_notifyRadEdit(pR);
        }
    }
    SAH_TRACEZ_OUT(ME);
}

/* Set our WPS data. This is usual fix at compiler time */
/** the object must point to an WIFI.wps_DefParam object !*/
void syncData_VendorWPS2OBJ(amxd_object_t* object, T_Radio* pR, int set) {
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    ASSERT_TRUE(debugIsRadPointer(pR), , ME, "Invalid rad ctx");
    T_CONST_WPS* pCWPS = pR->wpsConst;
    ASSERT_NOT_NULL(pCWPS, , ME, "NULL");
    SAH_TRACEZ_IN(ME);

    /* WPS stuff */
    if((set & SET)) {
        ASSERT_NOT_NULL(object, , ME, "NULL");

        amxd_trans_t trans;
        ASSERT_TRANSACTION_INIT(object, &trans, , ME, "%s : trans init failure", pR->Name);

        amxd_trans_set_cstring_t(&trans, "DefaultPin", pCWPS->DefaultPin);
        amxd_trans_set_cstring_t(&trans, "DevName", pCWPS->DevName);
        amxd_trans_set_cstring_t(&trans, "OUI", pCWPS->OUI);
        amxd_trans_set_cstring_t(&trans, "FriendlyName", pCWPS->FriendlyName);
        amxd_trans_set_cstring_t(&trans, "Manufacturer", pCWPS->Manufacturer);
        amxd_trans_set_cstring_t(&trans, "ManufacturerUrl", pCWPS->ManufacturerUrl);
        amxd_trans_set_cstring_t(&trans, "ModelDescription", pCWPS->ModelDescription);
        amxd_trans_set_cstring_t(&trans, "ModelName", pCWPS->ModelName);
        amxd_trans_set_cstring_t(&trans, "ModelNumber", pCWPS->ModelNumber);
        amxd_trans_set_cstring_t(&trans, "ModelUrl", pCWPS->ModelUrl);
        amxd_trans_set_cstring_t(&trans, "OsVersion", pCWPS->OsVersion);
        amxd_trans_set_cstring_t(&trans, "SerialNumber", pCWPS->SerialNumber);
        amxd_trans_set_cstring_t(&trans, "UUID", pCWPS->UUID);
        amxd_trans_set_int32_t(&trans, "wpsSupVer", pCWPS->wpsSupVer);
        amxd_trans_set_int32_t(&trans, "wpsUUIDShared", pCWPS->wpsUUIDShared);

        ASSERT_TRANSACTION_LOCAL_DM_END(&trans, , ME, "%s : trans apply failure", pR->Name);


    } else { /* Get! */
        amxc_var_t getVar;
        amxc_var_init(&getVar);
        /*
        ** Parameters are comming from the DeviceInfo!
        ** Be sure that the plugin is running before we're passing here.
        */
        int ret = amxb_get(get_wld_plugin_bus(), "DeviceInfo.", 0, &getVar, 1);
        if(ret == AMXB_STATUS_OK) {
            SAH_TRACEZ_INFO(ME, "Getting WPS Desc from DeviceInfo");
            amxc_var_t* devInfo = amxc_var_get_first(GET_ARG(&getVar, "0"));
            swl_str_copy(pCWPS->Manufacturer, sizeof(pCWPS->Manufacturer), GET_CHAR(devInfo, "Manufacturer"));
            swl_str_copy(pCWPS->ManufacturerUrl, sizeof(pCWPS->ManufacturerUrl), GET_CHAR(devInfo, "Manufacturer"));
            swl_str_copy(pCWPS->OUI, sizeof(pCWPS->OUI), GET_CHAR(devInfo, "ManufacturerOUI"));
            swl_str_copy(pCWPS->DevName, sizeof(pCWPS->DevName), GET_CHAR(devInfo, "DeviceName"));
#if CONFIG_USE_SAH_WPS_DEVICE_NAME
            swl_str_copy(pCWPS->DevName, sizeof(pCWPS->DevName), CONFIG_SAH_WPS_DEVICE_NAME);
#endif
            swl_str_copy(pCWPS->FriendlyName, sizeof(pCWPS->FriendlyName), GET_CHAR(devInfo, "FriendlyName"));
#if CONFIG_USE_SAH_WPS_FRIENDLY_NAME
            swl_str_copy(pCWPS->FriendlyName, sizeof(pCWPS->FriendlyName), CONFIG_SAH_WPS_FRIENDLY_NAME);
#endif
            swl_str_copy(pCWPS->ModelDescription, sizeof(pCWPS->ModelDescription), GET_CHAR(devInfo, "Description"));
            swl_str_copy(pCWPS->OsVersion, sizeof(pCWPS->OsVersion), GET_CHAR(devInfo, "SoftwareVersion"));
            swl_str_copy(pCWPS->SerialNumber, sizeof(pCWPS->SerialNumber), GET_CHAR(devInfo, "SerialNumber"));
            swl_str_copy(pCWPS->ModelName, sizeof(pCWPS->ModelName), GET_CHAR(devInfo, "ModelName"));
            swl_str_copy(pCWPS->ModelNumber, sizeof(pCWPS->ModelNumber), GET_CHAR(devInfo, "ModelNumber"));
            swl_str_copy(pCWPS->ModelUrl, sizeof(pCWPS->ModelUrl), GET_CHAR(devInfo, "VendorURL"));
        } else {
            SAH_TRACEZ_INFO(ME, "Getting WPS Desc from Environment");
            // Normally we're not passing here but in case generic build... we've some SAH data ;-)
            GETENV(pCWPS->DevName, "DevName");
#if CONFIG_USE_SAH_WPS_DEVICE_NAME
            swl_str_copy(pCWPS->DevName, sizeof(pCWPS->DevName), CONFIG_SAH_WPS_DEVICE_NAME);
#endif
            GETENV(pCWPS->OUI, "MANUFACTURER_OUI");
            GETENV(pCWPS->FriendlyName, "FRIENDLY_NAME");
#if CONFIG_USE_SAH_WPS_FRIENDLY_NAME
            swl_str_copy(pCWPS->FriendlyName, sizeof(pCWPS->FriendlyName), CONFIG_SAH_WPS_FRIENDLY_NAME);
#endif
            GETENV(pCWPS->Manufacturer, "MANUFACTURER");
            GETENV(pCWPS->ManufacturerUrl, "MANUFACTURER_URL");

            snprintf(pCWPS->ModelDescription, sizeof(pCWPS->ModelDescription),
                     "%s %s", (getenv("MODELNAME")) ? : "", (getenv("MANUFACTURER")) ? : "");

            GETENV(pCWPS->ModelName, "MODELNAME");
            GETENV(pCWPS->ModelNumber, "PRODUCT_CLASS");
            GETENV(pCWPS->ModelUrl, "MANUFACTURER_URL");
            GETENV(pCWPS->OsVersion, "OSVERSION");
            GETENV(pCWPS->SerialNumber, "SERIAL_NUMBER");
        }

        // WPS UUID not filled in? Do this only for each vendor!
        if(!pCWPS->UUID[0]) {

            char uuid[50] = {0};
            swl_uuid_t uuid_i = {{0}};
            char MACaddr[18] = {0};

            GETENV(MACaddr, "WAN_ADDR");

            uint8_t offset;
#ifdef CONFIG_USE_SAH_WPS_FORCE_EQUAL_WL_UUID
            offset = 0;
#else
            offset = pR->index;
#endif
            if((MACaddr[0] != 0) &&
               (swl_uuid_fromMacAddress(&uuid_i, MACaddr, offset) && swl_uuid_toChar(uuid, sizeof(uuid), &uuid_i))
               ) {
#ifdef CONFIG_USE_SAH_WPS_FORCE_EQUAL_WL_UUID
                pCWPS->wpsUUIDShared = 1;
#else
                pCWPS->wpsUUIDShared = 0;
#endif
                swl_str_copy(pCWPS->UUID, sizeof(pCWPS->UUID), uuid);
            } else {
                swl_str_copy(pCWPS->UUID, sizeof(pCWPS->UUID), "efe83912-97f4-4dcf-8a3f-8e5b27cddb9e");
                SAH_TRACEZ_ERROR(ME, "Could not create a valid UUID (uuid:'%s;), so we set a hardcoded valid UUID!!!!", uuid);
            }
            SAH_TRACEZ_INFO(ME, "UUID=%s", pCWPS->UUID);
        }
    }
    SAH_TRACEZ_OUT(ME);
}

amxd_status_t _FSM_Start(amxd_object_t* wifi,
                         amxd_function_t* func _UNUSED,
                         amxc_var_t* args,
                         amxc_var_t* retval) {
    int32_t ret = -1;

    amxd_status_t status = amxd_status_unknown_error;

    const char* vapname = GET_CHAR(args, "vap");
    ASSERT_STR(vapname, status, ME, "Missing vap name");
    amxc_var_t* var = GET_ARG(args, "bitnr");
    ASSERT_NOT_NULL(var, status, ME, "Missing bitnr");
    int32_t val_int32 = amxc_var_dyncast(int32_t, var);
    SAH_TRACEZ_INFO(ME, "vap %s bitnr %d", vapname, val_int32);

    amxd_object_t* objAP = amxd_object_findf(wifi, "AccessPoint.%s", vapname);
    ASSERT_NOT_NULL(objAP, status, ME, "Not vap instance for vapname %s", vapname);
    T_AccessPoint* pAP = (T_AccessPoint*) objAP->priv;
    ASSERT_TRUE(debugIsVapPointer(pAP), status, ME, "Not internal ctx from vapname %s", vapname);
    T_Radio* pR = pAP->pRadio;
    ASSERT_NOT_NULL(pR, status, ME, "No radio mapped to vapname %s", vapname);

    /* Set a bit in our state machine bit-array */
    setBitLongArray(pAP->fsm.FSM_BitActionArray, FSM_BW, (int) val_int32);

    SAH_TRACEZ_INFO(ME, "setBitLongArray");

    /* Start our state machine */
    if((ret = pR->pFA->mfn_wrad_fsm_state(pR)) == FSM_IDLE) {
        ret = pR->pFA->mfn_wrad_fsm(pR);
    }

    SAH_TRACEZ_INFO(ME, "mfn_wvap_fsm");
    if(ret == FSM_WAIT) {
        amxd_function_defer(func, &pR->call_id, retval, NULL, pR);
        return amxd_status_deferred;
    }

    amxc_var_set(int32_t, retval, ret);
    return (ret < 0) ? amxd_status_unknown_error : amxd_status_ok;
}

/**************************************************************************************************/
/* Only a button function for this VAP! SelfPIN is radio bounded Client and PBC not! */
amxd_status_t _wps_DefParam_wps_GenSelfPIN(amxd_object_t* obj _UNUSED,
                                           amxd_function_t* func _UNUSED,
                                           amxc_var_t* args _UNUSED,
                                           amxc_var_t* retval) {

    genSelfPIN();

    amxc_var_set(uint32_t, retval, 0);
    return amxd_status_ok;
}

/* Quick lookup of parameters we accept! */
const char* Radio_ObjParam[] = {
    "Alias",
    "OperatingFrequencyBand",
    "OperatingStandards",
    "Channel",
    "AutoChannelEnable",
    "AutoChannelRefreshPeriod",
    "OperatingChannelBandwidth",
    "ExtensionChannel",
    "GuardInterval",
    "MCS",
    "TransmitPower",
    "IEEE80211hEnabled",
    "RegulatoryDomain",
    "Enable",
    NULL
};

/**
 * disable an commit on an parameter enable toggle. (used for group config)
 */
amxd_status_t _edit(amxd_object_t* object,
                    amxd_function_t* func _UNUSED,
                    amxc_var_t* args _UNUSED,
                    amxc_var_t* retval) {
    T_Radio* pR = object->priv;
    bool success = false;

    /* Check our input data */
    if(pR && debugIsRadPointer(pR)) {
        pR->blockCommit = 1;
        pR->blockStart = swl_time_getMonoSec();
        success = true;
    }

    amxc_var_set(bool, retval, success);
    return amxd_status_ok;
}

typedef struct {
    int objtype;
    char* ObjStr;
    int stucttype;
    int OffSetOf;
} T_DEBUG_OBJ_STR;

T_DEBUG_OBJ_STR VerifyRadio[] = {
    {TPH_BOOL, "Enable", TPH_INT32, offsetof(T_Radio, enable)},
    {TPH_STR, "Status", TPH_INT32, offsetof(T_Radio, status)},
    {TPH_INT32, "Index", TPH_INT32, offsetof(T_Radio, index)},
    {TPH_BOOL, "AP_Mode", TPH_BOOL, offsetof(T_Radio, isAP)},
    {TPH_BOOL, "STA_Mode", TPH_BOOL, offsetof(T_Radio, isSTA)},
    {TPH_BOOL, "WDS_Mode", TPH_BOOL, offsetof(T_Radio, isWDS)},
    {TPH_BOOL, "WET_Mode", TPH_BOOL, offsetof(T_Radio, isWET)},
    {TPH_BOOL, "STASupported_Mode", TPH_BOOL, offsetof(T_Radio, isSTASup)},
    {TPH_BOOL, "WPS_Enrollee_Mode", TPH_BOOL, offsetof(T_Radio, isWPSEnrol)},
    {TPH_INT32, "MaxBitRate", TPH_INT32, offsetof(T_Radio, maxBitRate)},
    {TPH_STR, "SupportedFrequencyBands", TPH_INT32, offsetof(T_Radio, supportedFrequencyBands)},
    {TPH_STR, "OperatingFrequencyBand", TPH_INT32, offsetof(T_Radio, operatingFrequencyBand)},
    {TPH_STR, "SupportedStandards", TPH_INT32, offsetof(T_Radio, supportedStandards)},
    {TPH_STR, "OperatingStandards", TPH_INT32, offsetof(T_Radio, operatingStandards)},
    {TPH_STR, "OperatingStandardsFormat", TPH_INT32, offsetof(T_Radio, operatingStandardsFormat)},
    {TPH_UINT32, "OperatingClass", TPH_UINT32, offsetof(T_Radio, operatingClass)},
    {TPH_STR, "ChannelsInUse", TPH_STR, offsetof(T_Radio, channelsInUse)},
    {TPH_BOOL, "AutoChannelSupported", TPH_INT32, offsetof(T_Radio, autoChannelSupported)},
    {TPH_BOOL, "AutoChannelEnable", TPH_BOOL, offsetof(T_Radio, autoChannelEnable)},
    {TPH_INT32, "AutoChannelRefreshPeriod", TPH_INT32, offsetof(T_Radio, autoChannelRefreshPeriod)},
    {TPH_INT32, "Channel", TPH_INT32, offsetof(T_Radio, channel)},
    {TPH_STR, "OperatingChannelBandwidth", TPH_INT32, offsetof(T_Radio, operatingChannelBandwidth)},
    {TPH_STR, "CurrentOperatingChannelBandwidth", TPH_INT32, offsetof(T_Radio, runningChannelBandwidth)},
    {TPH_STR, "MaxChannelBandwidth", TPH_INT32, offsetof(T_Radio, maxChannelBandwidth)},
    {TPH_STR, "ExtensionChannel", TPH_INT32, offsetof(T_Radio, extensionChannel)},
    {TPH_BOOL, "ObssCoexistenceEnable", TPH_INT32, offsetof(T_Radio, obssCoexistenceEnabled)},
    {TPH_STR, "GuardInterval", TPH_INT32, offsetof(T_Radio, guardInterval)},
    {TPH_INT32, "MCS", TPH_INT32, offsetof(T_Radio, MCS)},
    {TPH_INT32, "TransmitPower", TPH_INT32, offsetof(T_Radio, transmitPower)},
    {TPH_INT32, "ActiveAntennaCtrl", TPH_INT32, offsetof(T_Radio, actAntennaCtrl)},
    {TPH_UINT32, "RetryLimit", TPH_UINT8, offsetof(T_Radio, retryLimit)},
    {TPH_UINT16, "RTSThreshold", TPH_UINT32, offsetof(T_Radio, rtsThreshold)},
    {TPH_UINT32, "LongRetryLimit", TPH_UINT8, offsetof(T_Radio, longRetryLimit)},
    {TPH_UINT32, "BeaconPeriod", TPH_UINT32, offsetof(T_Radio, beaconPeriod)},
    {TPH_UINT32, "DTIMPeriod", TPH_UINT32, offsetof(T_Radio, dtimPeriod)},
    {TPH_BOOL, "TargetWakeTimeEnable", TPH_BOOL, offsetof(T_Radio, twtEnable)},
    {TPH_BOOL, "OfdmaEnable", TPH_BOOL, offsetof(T_Radio, ofdmaEnable)},
    {TPH_STR, "HeCapsSupported", TPH_INT32, offsetof(T_Radio, heCapsSupported)},
    {TPH_STR, "HeCapsEnabled", TPH_INT32, offsetof(T_Radio, heCapsEnabled)},
    {TPH_BOOL, "IEEE80211hSupported", TPH_INT32, offsetof(T_Radio, IEEE80211hSupported)},
    {TPH_BOOL, "IEEE80211hEnabled", TPH_INT32, offsetof(T_Radio, setRadio80211hEnable)},
    {TPH_BOOL, "IEEE80211kSupported", TPH_BOOL, offsetof(T_Radio, IEEE80211kSupported)},
    {TPH_STR, "RegulatoryDomain", TPH_STR, offsetof(T_Radio, regulatoryDomain)},
    {TPH_BOOL, "ImplicitBeamFormingSupported", TPH_INT32, offsetof(T_Radio, implicitBeamFormingSupported)},
    {TPH_BOOL, "ImplicitBeamFormingEnabled", TPH_INT32, offsetof(T_Radio, implicitBeamFormingEnabled)},
    {TPH_BOOL, "ExplicitBeamFormingSupported", TPH_INT32, offsetof(T_Radio, explicitBeamFormingSupported)},
    {TPH_BOOL, "ExplicitBeamFormingEnabled", TPH_INT32, offsetof(T_Radio, explicitBeamFormingEnabled)},
    {TPH_STR, "RIFSEnabled", TPH_INT32, offsetof(T_Radio, RIFSEnabled)},
    {TPH_BOOL, "AirtimeFairnessEnabled", TPH_BOOL, offsetof(T_Radio, airtimeFairnessEnabled)},
    {TPH_BOOL, "RxPowerSaveEnabled", TPH_BOOL, offsetof(T_Radio, rxPowerSaveEnabled)},
    {TPH_BOOL, "MultiUserMIMOSupported", TPH_BOOL, offsetof(T_Radio, multiUserMIMOSupported)},
    {TPH_BOOL, "MultiUserMIMOEnabled", TPH_BOOL, offsetof(T_Radio, multiUserMIMOEnabled)},
    {TPH_INT32, "DFSChannelChangeEventCounter", TPH_UINT32, offsetof(T_Radio, DFSChannelChangeEventCounter)},
    {TPH_STR, "ChannelChangeReason", TPH_STR, offsetof(T_Radio, channelChangeReason)},
    {TPH_STR, "ChannelBandwidthChangeReason", TPH_STR, offsetof(T_Radio, channelBandwidthChangeReason)},
    {TPH_INT32, "ActiveAssociatedDevices", TPH_INT32, offsetof(T_Radio, currentStations)},
    {TPH_INT32, "ActiveVideoAssociatedDevices", TPH_INT32, offsetof(T_Radio, currentVideoStations)},
    {TPH_INT32, "MaxAssociatedDevices", TPH_INT32, offsetof(T_Radio, maxStations)},
    {TPH_BOOL, "KickRoamingStation", TPH_BOOL, offsetof(T_Radio, kickRoamStaEnabled)},
    {TPH_INT32, "dbgRADEnable", TPH_INT32, offsetof(T_Radio, dbgEnable)},
    {TPH_STR, "dbgRADFile", TPH_PSTR, offsetof(T_Radio, dbgOutput)},
    {0, NULL, 0, 0},
};

T_DEBUG_OBJ_STR VerifyAccessPoint[] = {
    {TPH_STR, "Alias", TPH_STR, offsetof(T_AccessPoint, alias)},
    {TPH_STR, "BridgeInterface", TPH_STR, offsetof(T_AccessPoint, bridgeName)},
    {TPH_STR, "SSIDReference", TPH_INT32, offsetof(T_AccessPoint, SSIDReference)},
    {TPH_BOOL, "SSIDAdvertisementEnabled", TPH_INT32, offsetof(T_AccessPoint, SSIDAdvertisementEnabled)},
    {TPH_INT32, "RetryLimit", TPH_INT32, offsetof(T_AccessPoint, retryLimit)},
    {TPH_BOOL, "WMMEnable", TPH_INT32, offsetof(T_AccessPoint, WMMEnable)},
    {TPH_BOOL, "UAPSDEnable", TPH_INT32, offsetof(T_AccessPoint, UAPSDEnable)},
    {TPH_BOOL, "IEEE80211kEnabled", TPH_BOOL, offsetof(T_AccessPoint, IEEE80211kEnable)},
    {TPH_STR, "Security.WEPKey", TPH_STR, offsetof(T_AccessPoint, WEPKey)},
    {TPH_STR, "Security.PreSharedKey", TPH_STR, offsetof(T_AccessPoint, preSharedKey)},
    {TPH_STR, "Security.KeyPassPhrase", TPH_STR, offsetof(T_AccessPoint, keyPassPhrase)},
    {TPH_STR, "Security.SAEPassphrase", TPH_STR, offsetof(T_AccessPoint, saePassphrase)},
    {TPH_INT32, "Security.RekeyingInterval", TPH_INT32, offsetof(T_AccessPoint, rekeyingInterval)},
    {TPH_STR, "Security.RadiusServerIPAddr", TPH_STR, offsetof(T_AccessPoint, radiusServerIPAddr)},
    {TPH_INT32, "Security.RadiusServerPort", TPH_INT32, offsetof(T_AccessPoint, radiusServerPort)},
    {TPH_STR, "Security.RadiusSecret", TPH_STR, offsetof(T_AccessPoint, radiusSecret)},
    {TPH_INT32, "Security.RadiusDefaultSessionTimeout", TPH_INT32, offsetof(T_AccessPoint, radiusDefaultSessionTimeout)},
    {TPH_STR, "Security.RadiusOwnIPAddress", TPH_STR, offsetof(T_AccessPoint, radiusOwnIPAddress)},
    {TPH_STR, "Security.RadiusNASIdentifier", TPH_STR, offsetof(T_AccessPoint, radiusNASIdentifier)},
    {TPH_STR, "Security.RadiusCalledStationId", TPH_STR, offsetof(T_AccessPoint, radiusCalledStationId)},
    {TPH_INT32, "Security.RadiusChargeableUserId", TPH_INT32, offsetof(T_AccessPoint, radiusChargeableUserId)},
    {TPH_STR, "Security.ModeEnabled", TPH_INT32, offsetof(T_AccessPoint, secModeEnabled)},
    {TPH_STR, "Security.MFPConfig", TPH_INT32, offsetof(T_AccessPoint, mfpConfig)},
    {TPH_INT32, "Security.SPPAmsdu", TPH_INT32, offsetof(T_AccessPoint, sppAmsdu)},
    {TPH_BOOL, "Security.SHA256Enable", TPH_INT32, offsetof(T_AccessPoint, SHA256Enable)},
    {TPH_BOOL, "Security.OWETransitionInterface", TPH_INT32, offsetof(T_AccessPoint, oweTransModeIntf)},
    {TPH_BOOL, "WPS.Enable", TPH_INT32, offsetof(T_AccessPoint, WPS_Enable)},
    {TPH_STR, "WPS.ConfigMethodsEnabled", TPH_INT32, offsetof(T_AccessPoint, WPS_ConfigMethodsEnabled)},
    {TPH_BOOL, "WPS.Configured", TPH_INT32, offsetof(T_AccessPoint, WPS_Configured)},
    {TPH_BOOL, "WPS.PairingInProgress", TPH_BOOL, offsetof(T_AccessPoint, wpsSessionInfo) + offsetof(wld_wpsSessionInfo_t, WPS_PairingInProgress)},
    {TPH_BOOL, "Enable", TPH_INT32, offsetof(T_AccessPoint, enable)},
    {TPH_BOOL, "Status", TPH_INT32, offsetof(T_AccessPoint, status)},
    {TPH_INT32, "dbgAPEnable", TPH_INT32, offsetof(T_AccessPoint, dbgEnable)},
    {TPH_BOOL, "WDSEnable", TPH_BOOL, offsetof(T_AccessPoint, wdsEnable)},
    {TPH_STR, "dbgAPFile", TPH_PSTR, offsetof(T_AccessPoint, dbgOutput)},
    {TPH_INT32, "MaxAssociatedDevices", TPH_INT32, offsetof(T_AccessPoint, MaxStations)},
    {TPH_INT32, "ActiveAssociatedDeviceNumberOfEntries", TPH_INT32, offsetof(T_AccessPoint, ActiveAssociatedDeviceNumberOfEntries)},
    {TPH_SEL_SSID, "SWAP POINTER TO SSID", TPH_SEL_SSID, 0},
    {TPH_STR, "BSSID", TPH_BSTR, offsetof(T_SSID, BSSID)},
    {TPH_STR, "SSID", TPH_STR, offsetof(T_SSID, SSID)},
    {TPH_BOOL, "Enable", TPH_BOOL, offsetof(T_SSID, enable)},
    {TPH_STR, "Status", TPH_INT32, offsetof(T_SSID, status)},
    {0, NULL, 0, 0},
};


/* Get the data from object and structure... (without changing any value)*/
int DebugObjStructPrint(void* pData) {
    amxd_object_t* selObj = NULL;
    T_Radio* pR = NULL;
    T_AccessPoint* pAP = NULL;
    char* pHR = NULL;
    char* pTmp = NULL;
    int i, val_int;
    bool valBool;
    uint32_t val_uint = 0;
    char fullbuffer[128];
    char objbuf[256];
    char structbuf[256];

    pR = (T_Radio*) pData;

    /* Debug Radio part...*/
    for(i = 0; VerifyRadio[i].ObjStr; i++) {
        pHR = (char*) pData;    // Need it for offsetoff macro
        switch(VerifyRadio[i].stucttype) {
        case TPH_STR:
            sprintf(structbuf, "T_Radio = %32s", (char*) &pHR[VerifyRadio[i].OffSetOf]);
            break;
        case TPH_PSTR:
            sprintf(structbuf, "T_Radio = %32s", *((char**) &pHR[VerifyRadio[i].OffSetOf]));
            break;
        case TPH_INT32:
            val_int = *((long*) &pHR[VerifyRadio[i].OffSetOf]);
            sprintf(structbuf, "T_Radio = %i", val_int);
            break;
        case TPH_INT16:
            val_int = *((int16_t*) &pHR[VerifyRadio[i].OffSetOf]);
            sprintf(structbuf, "T_Radio = %i", val_int);
            break;
        case TPH_INT8:
            val_int = *((int8_t*) &pHR[VerifyRadio[i].OffSetOf]);
            sprintf(structbuf, "T_Radio = %i", val_int);
            break;
        case TPH_UINT32:
            val_uint = *((long*) &pHR[VerifyRadio[i].OffSetOf]);
            sprintf(structbuf, "T_Radio = %u", val_uint);
            break;
        case TPH_UINT16:
            val_uint = *((uint16_t*) &pHR[VerifyRadio[i].OffSetOf]);
            sprintf(structbuf, "T_Radio = %u", val_uint);
            break;
        case TPH_UINT8:
            val_uint = *((uint8_t*) &pHR[VerifyRadio[i].OffSetOf]);
            sprintf(structbuf, "T_Radio = %u", val_uint);
            break;
        case TPH_BOOL:
            val_int = *((bool*) &pHR[VerifyRadio[i].OffSetOf]);
            sprintf(structbuf, "T_Radio = %u", val_int);
            break;
        default:
            sprintf(structbuf, "T_Radio = ERROR %u", VerifyRadio[i].stucttype);
            break;
        }

        switch(VerifyRadio[i].objtype) {
        case TPH_BOOL:
            valBool = amxd_object_get_bool(pR->pBus, VerifyRadio[i].ObjStr, NULL);
            sprintf(objbuf, "Object %s = %u", VerifyRadio[i].ObjStr, valBool);
            break;
        case TPH_INT32:
            val_int = amxd_object_get_int32_t(pR->pBus, VerifyRadio[i].ObjStr, NULL);
            sprintf(objbuf, "Object %s = %i", VerifyRadio[i].ObjStr, val_int);
            break;
        case TPH_UINT32:
            val_uint = amxd_object_get_uint32_t(pR->pBus, VerifyRadio[i].ObjStr, NULL);
            sprintf(objbuf, "Object %s = %u", VerifyRadio[i].ObjStr, val_uint);
            break;
        case TPH_STR:
            swl_str_copy(fullbuffer,
                         sizeof(fullbuffer),
                         amxd_object_get_cstring_t(pR->pBus, VerifyRadio[i].ObjStr, NULL));
            sprintf(objbuf, "Object %s = %s", VerifyRadio[i].ObjStr, fullbuffer);
            break;
        default:
            sprintf(objbuf, "Object %s = ERROR %u", VerifyRadio[i].ObjStr, VerifyRadio[i].objtype);
            break;
        }
        SAH_TRACEZ_ERROR(ME, "%s:%s ; %s", pR->Name, objbuf, structbuf);
    }
    /* FSM Radio data */
    SAH_TRACEZ_ERROR(ME, "Radio FSM_BitActionArray     %s - %d - %08x | %08x",
                     pR->Name,
                     pR->fsm_radio_st,
                     (unsigned int) pR->fsmRad.FSM_BitActionArray[0],
                     (unsigned int) pR->fsmRad.FSM_BitActionArray[1]);
    SAH_TRACEZ_ERROR(ME, "Radio FSM_AC_BitActionArray  %s - %d - %08x | %08x",
                     pR->Name,
                     pR->fsm_radio_st,
                     (unsigned int) pR->fsmRad.FSM_AC_BitActionArray[0],
                     (unsigned int) pR->fsmRad.FSM_AC_BitActionArray[1]);
    SAH_TRACEZ_ERROR(ME, "FSM_State/Loop/ComPend/TODC/timer %s - %08x | %08x | %08x | %08x | %08x | %u",
                     pR->Name,
                     pR->fsmRad.FSM_State,
                     pR->fsmRad.FSM_Loop,
                     pR->fsmRad.FSM_ComPend,
                     pR->fsmRad.TODC,
                     pR->fsmRad.timeout_msec,
                     pR->fsmRad.FSM_SyncAll);
    SAH_TRACEZ_ERROR(ME, "FSM_Delay/Retry/SrcVAP/DC/timer %s - %08x | %08x | %08x | %p | %p",
                     pR->Name,
                     pR->fsmRad.FSM_Delay,
                     pR->fsmRad.FSM_Retry,
                     pR->fsmRad.FSM_SrcVAP,
                     pR->fsmRad.obj_DelayCommit,
                     pR->fsmRad.timer);
    SAH_TRACEZ_ERROR(ME, "BloCom %u State %u",
                     pR->blockCommit, pR->pFA->mfn_wrad_fsm_state(pR));

    /* Do this also for the VAP's */
    wld_rad_forEachAp(pAP, pR) {
        pHR = (char*) pAP;
        selObj = pAP->pBus;

        /* FSM VAP data */
        SAH_TRACEZ_ERROR(ME, "VAP FSM_BitActionArray     %s - %08x | %08x",
                         pAP->alias,
                         (unsigned int) pAP->fsm.FSM_BitActionArray[0],
                         (unsigned int) pAP->fsm.FSM_BitActionArray[1]);
        SAH_TRACEZ_ERROR(ME, "VAP FSM_AC_BitActionArray  %s - %08x | %08x",
                         pAP->alias,
                         (unsigned int) pAP->fsm.FSM_AC_BitActionArray[0],
                         (unsigned int) pAP->fsm.FSM_AC_BitActionArray[1]);
        SAH_TRACEZ_ERROR(ME, "FSM_State/Loop/ComPend/timer %s - %08x | %08x | %p",
                         pAP->alias,
                         pAP->fsm.FSM_State,
                         pAP->fsm.FSM_Loop,
                         pAP->fsm.timer);

        for(i = 0; VerifyAccessPoint[i].ObjStr; i++) {
            switch(VerifyAccessPoint[i].stucttype) {
            case TPH_STR:
                sprintf(structbuf, "T_AP = %32s", (char*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                break;
            case TPH_BSTR:
                pTmp = (char*) &pHR[VerifyAccessPoint[i].OffSetOf];
                sprintf(structbuf, "T_AP = %02x%02x%02x%02x...", pTmp[0], pTmp[1], pTmp[2], pTmp[3]);
                break;
            case TPH_PSTR:
                sprintf(structbuf, "T_Radio = %32s", *((char**) &pHR[VerifyAccessPoint[i].OffSetOf]));
                break;
            case TPH_INT32:
                val_int = *((int32_t*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                sprintf(structbuf, "T_AP = %i", val_int);
                break;
            case TPH_INT16:
                val_int = *((int16_t*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                sprintf(structbuf, "T_Radio = %i", val_int);
                break;
            case TPH_INT8:
                val_int = *((int8_t*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                sprintf(structbuf, "T_Radio = %i", val_int);
                break;
            case TPH_UINT32:
                val_uint = *((uint32_t*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                sprintf(structbuf, "T_AP = %u", val_uint);
                break;
            case TPH_UINT16:
                val_uint = *((int16_t*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                sprintf(structbuf, "T_Radio = %u", val_uint);
                break;
            case TPH_UINT8:
                val_uint = *((int8_t*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                sprintf(structbuf, "T_Radio = %u", val_uint);
                break;
            case TPH_BOOL:
                val_int = *((bool*) &pHR[VerifyAccessPoint[i].OffSetOf]);
                sprintf(structbuf, "T_AP = %u", val_int);
                break;
            case TPH_SEL_SSID:
                pHR = (char*) pAP->pSSID;
                selObj = ((T_SSID*) (pAP->pSSID))->pBus;
                continue;   // Go to next item...
                break;
            default:
                sprintf(structbuf, "T_AP = ERROR %u", VerifyAccessPoint[i].stucttype);
                break;

            }

            switch(VerifyAccessPoint[i].objtype) {
            case TPH_BOOL:
                valBool = amxd_object_get_bool(selObj, VerifyAccessPoint[i].ObjStr, NULL);
                sprintf(objbuf, "Object %s = %u", VerifyAccessPoint[i].ObjStr, valBool);
                break;
            case TPH_INT32:
                val_int = amxd_object_get_int32_t(selObj, VerifyAccessPoint[i].ObjStr, NULL);
                sprintf(objbuf, "Object %s = %i", VerifyAccessPoint[i].ObjStr, val_int);
                break;
            case TPH_UINT32:
                val_int = amxd_object_get_int32_t(selObj, VerifyAccessPoint[i].ObjStr, NULL);
                sprintf(objbuf, "Object %s = %u", VerifyAccessPoint[i].ObjStr, val_int);
                break;
            case TPH_STR:
                swl_str_copy(fullbuffer,
                             sizeof(fullbuffer),
                             amxd_object_get_cstring_t(selObj, VerifyAccessPoint[i].ObjStr, NULL));
                sprintf(objbuf, "Object %s = %s", VerifyAccessPoint[i].ObjStr, fullbuffer);
                break;
            default:
                sprintf(objbuf, "Object = ERROR %u", VerifyAccessPoint[i].objtype);
                break;
            }
            SAH_TRACEZ_ERROR(ME, "%s:%s ; %s", pAP->alias, objbuf, structbuf);
        }

    }

    return 0;
}

void Radio_Commit_Abort(uint64_t call_id _UNUSED, void* userdata) {
    T_Radio* pR = (T_Radio*) userdata;
    ASSERTS_NOT_NULL(pR, , ME, "NULL");
    pR->call_id = 0;
}

static void doRadioReset(T_Radio* pRad) {
    /* Looks we're hanging? free all timer/callbacks ! */
    SAH_TRACEZ_ERROR(ME, "Radio reset %s ", pRad->Name);
    int fsmState = pRad->pFA->mfn_wrad_fsm_state(pRad);
    SAH_TRACEZ_ERROR(ME, "BloCom %u State %u", pRad->blockCommit, fsmState);
    T_AccessPoint* pAP;
    /* collect all dependencies on all attached ep interfaces, mirror it on the RAD interface */
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pRad) {
        SAH_TRACEZ_ERROR(ME, "Reset ep %s: 0x%lx 0x%lx // 0x%lx 0x%lx ",
                         pEP->alias,
                         pEP->fsm.FSM_BitActionArray[0], pEP->fsm.FSM_BitActionArray[1],
                         pEP->fsm.FSM_AC_BitActionArray[0], pEP->fsm.FSM_AC_BitActionArray[1]);
    }

    /* collect all dependencies on all attached vap interfaces, mirror it on the RAD interface */
    wld_rad_forEachAp(pAP, pRad) {
        SAH_TRACEZ_ERROR(ME, "Reset ap %s: 0x%lx 0x%lx // 0x%lx 0x%lx",
                         pAP->alias,
                         pAP->fsm.FSM_BitActionArray[0], pAP->fsm.FSM_BitActionArray[1],
                         pAP->fsm.FSM_AC_BitActionArray[0], pAP->fsm.FSM_AC_BitActionArray[1]);
    }

    if(pRad->call_id != 0) {
        fsm_delay_reply(pRad->call_id, amxd_status_unknown_error, NULL);
        pRad->call_id = 0;
    }
    if(pRad->fsmRad.timer) {
        amxp_timer_delete(&pRad->fsmRad.timer);
    }
    pRad->pFA->mfn_wrad_fsm_reset(pRad);
    pRad->fsmRad.timer = 0;
    /* Reset full FSM */
    pRad->fsm_radio_st = FSM_IDLE;
    pRad->fsmRad.FSM_Loop = 0;
    pRad->fsmRad.FSM_Delay = 0;
    pRad->fsmRad.FSM_State = 0;
    pRad->fsmRad.FSM_ComPend = 0;
    // Force full sync
    pRad->fsmRad.FSM_SyncAll = 1;
    // Start the fsm again, so we can start working again
    pRad->pFA->mfn_wrad_fsm(pRad);

    //increment counter to show we reset
    wld_rad_incrementCounterStr(pRad, &pRad->genericCounters, WLD_RAD_EV_FSM_RESET, "0x%x", fsmState);
}


static void doAllRadioReset() {
    T_Radio* pRad = NULL;
    wld_for_eachRad(pRad) {
        if(pRad->fsm_radio_st != FSM_IDLE) {
            doRadioReset(pRad);
        }
    }
}

/**
 * The function becomes active after a commit and will be called
 * for updating some object values that could changed in time! The
 * choice of parameters that must be updated is fully controlled
 * by the vendor plugin.
 */
static void rad_delayed_commit_time_handler(amxp_timer_t* timer, void* userdata) {
    T_Radio* pR = (T_Radio*) userdata;
    (void) timer;

    SAH_TRACEZ_IN(ME);
    // By every cycle inc the delay till 2 minutes delay... we keep running.
    pR->fsmRad.TODC += (pR->fsmRad.TODC >= 120000) ? 0 : +10000;

    // Call the function...
    if(pR->pFA->mfn_wrad_fsm_delay_commit) {
        pR->pFA->mfn_wrad_fsm_delay_commit(pR);
        if(pR->fsmRad.TODC) {
            amxp_timer_start(pR->fsmRad.obj_DelayCommit, pR->fsmRad.TODC);
        }
    } else {
        pR->fsmRad.TODC = 0; // No use for updates
    }
    // Clearup timer when done!
    if(!pR->fsmRad.TODC) {
        amxp_timer_delete(&pR->fsmRad.obj_DelayCommit);
        pR->fsmRad.obj_DelayCommit = NULL;
    }
    SAH_TRACEZ_OUT(ME);
}

void wld_rad_doSync(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    pRad->pFA->mfn_wrad_sync(pRad, SET);
    wld_autoCommitMgr_notifyRadEdit(pRad);
}

amxd_status_t _sync(amxd_object_t* object,
                    amxd_function_t* func _UNUSED,
                    amxc_var_t* args _UNUSED,
                    amxc_var_t* retval _UNUSED) {

    T_Radio* pR = object->priv;
    wld_rad_doSync(pR);

    return amxd_status_ok;
}

/**
 * Trigger the "delay commit handler".
 *
 * If restartIfActive is false, then if the delay commit timer is already started, nothing will happen.
 * If restartIfActive is true, then delay commit started will be restarted if already running.
 */
void wld_rad_triggerDelayCommit(T_Radio* pRad, uint32_t delay, bool restartIfActive) {
    /* Start timer for late commit update... */
    if(!pRad->pFA->mfn_wrad_fsm_delay_commit) {
        return;
    }

    amxp_timer_state_t timerState = amxp_timer_get_state(pRad->fsmRad.obj_DelayCommit);
    if(((timerState == amxp_timer_started ) || (timerState == amxp_timer_running))
       && !restartIfActive) {
        return;
    }

    pRad->fsmRad.TODC = delay; /* 10 seconds */
    if(!pRad->fsmRad.obj_DelayCommit) {
        amxp_timer_new(&pRad->fsmRad.obj_DelayCommit, rad_delayed_commit_time_handler, pRad);
        if(pRad->fsmRad.obj_DelayCommit != NULL) {
            SAH_TRACEZ_INFO(ME, "Create delayCommit timer %s", pRad->Name);
        } else {
            SAH_TRACEZ_ERROR(ME, "Failed to create delay commit timer %s", pRad->Name);
        }
    }
    SAH_TRACEZ_INFO(ME, "Start delayCommit callback %s - %d", pRad->Name, pRad->fsmRad.TODC);
    amxp_timer_start(pRad->fsmRad.obj_DelayCommit, pRad->fsmRad.TODC);

}

int wld_rad_doRadioCommit(T_Radio* pRad) {
    int ret = -1;
    if(!pRad || !debugIsRadPointer(pRad)) {
        SAH_TRACEZ_ERROR(ME, "Invalid commit");
        return -1;
    }

    wld_rad_triggerDelayCommit(pRad, 10000, true);

    if((pRad->fsm_radio_st == FSM_IDLE) && !pRad->fsmRad.timer) {
        ret = pRad->pFA->mfn_wrad_fsm(pRad);
    } else {
        /* Mark as Commit Pending */
        swl_timeMono_t now = swl_time_getMonoSec();

        if(pRad->fsmRad.FSM_ComPend == 0) {
            pRad->fsmRad.FSM_ComPend_Start = now;
        }

        time_t diff = now - pRad->fsmRad.FSM_ComPend_Start;

        pRad->fsmRad.FSM_ComPend++;
        SAH_TRACEZ_INFO(ME, "%s commit pending (%d)!", pRad->Name, pRad->fsmRad.FSM_ComPend);
        // We have 50 pending commits, and waited for longer as 30 seconds.
        if((pRad->fsmRad.FSM_ComPend > 50) && (diff > 30)) {
            SAH_TRACEZ_WARNING(ME, "%s FSM RESET!", pRad->Name);
            doAllRadioReset();
        }
        ret = 0;
    }

    if(ret < 0) {
        SAH_TRACEZ_ERROR(ME, "Failed commit on %s = %d", pRad->Name, ret);
    } else {
        SAH_TRACEZ_INFO(ME, "Success commit on %s = %d", pRad->Name, ret);
    }

    return ret;
}

#define BLOCK_COMMIT_TIMEOUT 60

int wld_rad_doCommitIfUnblocked(T_Radio* pRad) {
    int ret = 0;
    if(!pRad->blockCommit) {
        ret = wld_rad_doRadioCommit(pRad);
    } else {
        swl_timeMono_t now = swl_time_getMonoSec();
        uint32_t diff = (now - pRad->blockStart);
        if(diff > BLOCK_COMMIT_TIMEOUT) {
            SAH_TRACEZ_ERROR(ME, "Ignore commit block @ %s because time expired %u", pRad->Name, diff);
            ret = wld_rad_doRadioCommit(pRad);
        } else if(now < pRad->blockStart) {
            SAH_TRACEZ_ERROR(ME, "Block start in future %s, ignore", pRad->Name);
            ret = wld_rad_doRadioCommit(pRad);
        } else {
            SAH_TRACEZ_NOTICE(ME, "Skipping commit due to block %s, blocked for %u", pRad->Name, diff);
        }
    }
    return ret;
}

amxd_status_t _Radio_commit(amxd_object_t* object,
                            amxd_function_t* func _UNUSED,
                            amxc_var_t* args _UNUSED,
                            amxc_var_t* retval) {

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, amxd_status_unknown_error, ME, "NULL");

    pR->blockCommit = false;
    int ret = wld_rad_doRadioCommit(pR);

    if(ret == FSM_WAIT) {
        amxd_function_defer(func, &pR->call_id, retval, Radio_Commit_Abort, pR);
        return amxd_status_deferred;
    }

    amxc_var_set(int32_t, retval, ret);
    return amxd_status_ok;
}



amxd_status_t _startAutoChannelSelection(amxd_object_t* object,
                                         amxd_function_t* func _UNUSED,
                                         amxc_var_t* args _UNUSED,
                                         amxc_var_t* ret _UNUSED) {

    T_Radio* pR = object->priv;
    if(!pR || !debugIsRadPointer(pR)) {
        SAH_TRACEZ_ERROR(ME, "%s is not valid Radio", amxd_object_get_name(object, AMXD_OBJECT_NAMED));
        return amxd_status_unknown_error;
    }
    if(!amxd_object_get_bool(object, "Enable", NULL)) {
        SAH_TRACEZ_ERROR(ME, "%s: Radio is disabled", pR->Name);
        return amxd_status_unknown_error;
    }
    if(!amxd_object_get_bool(object, "AutoChannelSupported", NULL)) {
        SAH_TRACEZ_ERROR(ME, "%s: AutoChannel is not supported", pR->Name);
        return amxd_status_unknown_error;
    }
    if(!amxd_object_get_bool(object, "AutoChannelEnable", NULL)) {
        SAH_TRACEZ_ERROR(ME, "%s: AutoChannel is not enabled", pR->Name);
        return amxd_status_unknown_error;
    }
    if(amxd_object_get_bool(object, "AutoChannelSelecting", NULL)) {
        SAH_TRACEZ_ERROR(ME, "%s: AutoChannel selection is already running", pR->Name);
        return amxd_status_unknown_error;
    }
    // Try first 'direct' path to start AutoChannel.
    if(pR->pFA->mfn_wrad_startacs(pR, SET | MVRADIO) < 0) {
        // Use emulation approach by kicking the FSM again.
        pR->pFA->mfn_wrad_autochannelenable(pR, true, SET);
        wld_rad_doRadioCommit(pR);
    }
    return amxd_status_ok;
}

amxd_status_t _startACS(amxd_object_t* obj,
                        amxd_function_t* func _UNUSED,
                        amxc_var_t* args,
                        amxc_var_t* ret _UNUSED) {
    return _startAutoChannelSelection(obj, func, args, ret);
}

amxd_status_t _checkWPSPIN(amxd_object_t* obj _UNUSED,
                           amxd_function_t* func _UNUSED,
                           amxc_var_t* args,
                           amxc_var_t* retval) {
    unsigned long PIN = 0;

    int32_t ret = -1;

    /*     bool checkWPSPIN(string PIN); */
    const char* pin = GET_CHAR(args, "PIN");
    char pinModified[16] = {'\0'};
    snprintf(pinModified, sizeof(pinModified), "%s", pin);

    SAH_TRACEZ_INFO(ME, "pin:%s", pinModified);
    stripOutToken(pinModified, "-");
    stripOutToken(pinModified, " ");
    if(sscanf(pinModified, "%lu", &PIN) == 1) {
        SAH_TRACEZ_INFO(ME, "PIN: %s %lu", pinModified, PIN);
        ret = wldu_checkWpsPinStr(pinModified);
    } else {
        SAH_TRACEZ_ERROR(ME, "PIN: '%s' sscanf did not return 1", pinModified);
    }

    amxc_var_set(int32_t, retval, ret);

    return (ret < 0) ? amxd_status_unknown_error : amxd_status_ok;
}

/* Find first vap from radio */
T_AccessPoint* wld_rad_getFirstVap(T_Radio* pR) {
    ASSERT_NOT_NULL(pR, NULL, ME, "NULL");
    amxc_llist_it_t* it = (amxc_llist_it_t*) amxc_llist_get_first(&pR->llAP);
    ASSERTI_NOT_NULL(it, NULL, ME, "Empty");
    return (T_AccessPoint*) amxc_llist_it_get_data(it, T_AccessPoint, it);
}

T_EndPoint* wld_rad_getFirstEp(T_Radio* pR) {
    ASSERT_NOT_NULL(pR, NULL, ME, "NULL");
    amxc_llist_it_t* it = (amxc_llist_it_t*) amxc_llist_get_first(&pR->llEndPoints);
    ASSERTI_NOT_NULL(it, NULL, ME, "Empty");
    return (T_EndPoint*) amxc_llist_it_get_data(it, T_EndPoint, it);
}

T_AccessPoint* wld_rad_getFirstEnabledVap(T_Radio* pR) {
    ASSERT_NOT_NULL(pR, NULL, ME, "NULL");
    T_AccessPoint* pAP = NULL;
    wld_rad_forEachAp(pAP, pR) {
        if(pAP->enable && pAP->pSSID && pAP->pSSID->enable) {
            return pAP;
        }
    }
    return NULL;
}

uint32_t wld_rad_countEnabledVaps(T_Radio* pR) {
    uint32_t count = 0;
    ASSERT_NOT_NULL(pR, count, ME, "NULL");
    T_AccessPoint* pAP = NULL;
    wld_rad_forEachAp(pAP, pR) {
        if(pAP->enable && pAP->pSSID && pAP->pSSID->enable) {
            count++;
        }
    }
    return count;
}

wld_mbssidAdvertisement_mode_e wld_rad_getMbssidAdsMode(T_Radio* pRad) {
    wld_mbssidAdvertisement_mode_e mode = MBSSID_ADVERTISEMENT_MODE_OFF;
    ASSERTS_NOT_NULL(pRad, mode, ME, "NULL");
    swl_radioStandard_m stds = pRad->supportedStandards;
    if(!SWL_BIT_IS_ONLY_SET(pRad->operatingStandards, SWL_RADSTD_AUTO)) {
        stds &= pRad->operatingStandards;
    }
    if(swl_bit32_getHighest(stds) >= SWL_RADSTD_AX) {
        if(pRad->mbssidAdsMode == MBSSID_ADVERTISEMENT_MODE_AUTO) {
            if(wld_rad_is_6ghz(pRad)) {
                int32_t hMode = swl_bit32_getHighest(pRad->suppMbssidAdsModes);
                if((hMode > -1) && (hMode < MBSSID_ADVERTISEMENT_MODE_MAX)) {
                    mode = (wld_mbssidAdvertisement_mode_e) hMode;
                }
            }
        } else if(SWL_BIT_IS_SET(pRad->suppMbssidAdsModes, pRad->mbssidAdsMode)) {
            mode = pRad->mbssidAdsMode;
        }
    }
    return mode;
}

bool wld_rad_hasMbssidAds(T_Radio* pRad) {
    return (wld_rad_getMbssidAdsMode(pRad) != MBSSID_ADVERTISEMENT_MODE_OFF);
}

/* find VAP with matching name */
T_AccessPoint* wld_rad_vap_from_name(T_Radio* pR, const char* ifname) {
    T_AccessPoint* pAP = NULL;

    wld_rad_forEachAp(pAP, pR) {
        if(!strcmp((const char*) pAP->alias, ifname)) {
            return pAP;
        }
    }
    return NULL;
}

/* find VAP with matching WDS name */
T_AccessPoint* wld_rad_vap_from_wds_name(T_Radio* pR, const char* ifname) {
    T_AccessPoint* pAP = NULL;

    wld_rad_forEachAp(pAP, pR) {
        amxc_llist_for_each(it, &pAP->llIntfWds) {
            wld_wds_intf_t* wdsIntf = amxc_llist_it_get_data(it, wld_wds_intf_t, entry);
            if(swl_str_matches(ifname, wdsIntf->name)) {
                return pAP;
            }
        }
    }
    return NULL;
}

/* find EP with matching name */
T_EndPoint* wld_rad_ep_from_name(T_Radio* pR, const char* ifname) {
    T_EndPoint* pEP = NULL;

    wld_rad_forEachEp(pEP, pR) {
        if(!strcmp((const char*) pEP->Name, ifname)) {
            return pEP;
        }
    }
    return NULL;
}

T_Radio* wld_rad_from_name(const char* ifname) {
    T_Radio* pR;

    wld_for_eachRad(pR) {
        if(!strcmp((const char*) pR->Name, ifname)) {
            return pR;
        }
    }

    return NULL;
}

/* find VAP with matching name over all the radios */
T_AccessPoint* wld_vap_from_name(const char* ifname) {
    T_Radio* pR;
    T_AccessPoint* pAP = NULL;

    wld_for_eachRad(pR) {
        if((pAP = wld_rad_vap_from_name(pR, ifname))) {
            break;
        }
    }

    return pAP;
}

/* find VAP with matching name over all the radios */
T_EndPoint* wld_vep_from_name(const char* ifname) {
    T_Radio* pR;
    T_EndPoint* pEP = NULL;

    wld_for_eachRad(pR) {
        if((pEP = wld_rad_ep_from_name(pR, ifname))) {
            break;
        }
    }

    return pEP;
}

/*
 * @brief check whether object is valid radio obj instance
 */
bool wld_rad_isRadObj(amxd_object_t* radObj) {
    ASSERTS_EQUALS(amxd_object_get_type(radObj), amxd_object_instance, false, ME, "Not instance");
    amxd_object_t* parentObj = amxd_object_get_parent(radObj);
    ASSERT_EQUALS(get_wld_object(), amxd_object_get_parent(parentObj), false, ME, "wrong location");
    const char* parentName = amxd_object_get_name(parentObj, AMXD_OBJECT_NAMED);
    ASSERT_TRUE(swl_str_matches(parentName, "Radio"), false, ME, "invalid parent obj(%s)", parentName);
    return true;
}
/*
 * @brief return radio ctx of radio object
 */
T_Radio* wld_rad_fromObj(amxd_object_t* radObj) {
    ASSERTS_TRUE(wld_rad_isRadObj(radObj), NULL, ME, "Not rad obj instance");
    T_Radio* pRad = (T_Radio*) radObj->priv;
    ASSERTI_NOT_NULL(pRad, NULL, ME, "NULL");
    ASSERT_TRUE(debugIsRadPointer(pRad), NULL, ME, "INVALID");
    return pRad;
}

/*
 * @brief return radio ctx of previous radio object
 */
T_Radio* wld_rad_prevRadFromObj(amxd_object_t* radObj) {
    ASSERTS_TRUE(wld_rad_isRadObj(radObj), NULL, ME, "ref is not radio obj");
    return wld_rad_fromObj(wld_util_getPrevObjInst(radObj));
}

/*
 * @brief return radio ctx of previous radio list node
 */
T_Radio* wld_rad_prevRadFromList(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, NULL, ME, "NULL");
    amxc_llist_it_t* it = amxc_llist_it_get_previous(&pRad->it);
    ASSERTS_NOT_NULL(it, NULL, ME, "No previous rad ctx");
    return amxc_container_of(it, T_Radio, it);
}

/*
 * @brief return radio ctx of next radio object
 */
T_Radio* wld_rad_nextRadFromObj(amxd_object_t* radObj) {
    ASSERTS_TRUE(wld_rad_isRadObj(radObj), NULL, ME, "ref is not radio obj");
    return wld_rad_fromObj(wld_util_getNextObjInst(radObj));
}

/*
 * @brief return radio ctx of next radio list node
 */
T_Radio* wld_rad_nextRadFromList(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, NULL, ME, "NULL");
    amxc_llist_it_t* it = amxc_llist_it_get_next(&pRad->it);
    ASSERTS_NOT_NULL(it, NULL, ME, "No previous rad ctx");
    return amxc_container_of(it, T_Radio, it);
}

bool wld_radio_notify_scanresults(amxd_object_t* obj) {
    const uint32_t type = NOTIFY_SCAN_DONE;
    const char* const name = "ScanComplete";
    SAH_TRACEZ_INFO(ME, "send notification [%d|%s]", type, name);
    amxd_object_trigger_signal(obj, name, NULL);
    return true;
}

amxd_status_t _getRadioAirStats(amxd_object_t* object,
                                amxd_function_t* func _UNUSED,
                                amxc_var_t* args _UNUSED,
                                amxc_var_t* retval_map) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERT_NOT_NULL(pR, amxd_status_ok, ME, "Not mapped to radio ctx");

    ASSERTI_TRUE(wld_rad_isActive(pR), amxd_status_ok, ME, "%s : not ready", pR->Name);

    wld_airStats_t stats;
    bzero(&stats, sizeof(stats));

    amxc_var_t vendorStats;
    amxc_var_init(&vendorStats);
    stats.vendorStats = &vendorStats;

    swl_rc_ne ret = pR->pFA->mfn_wrad_airstats(pR, &stats);

    if(ret < SWL_RC_OK) {
        SAH_TRACEZ_ERROR(ME, "Failed to get Air statistics");
        amxc_var_clean(&vendorStats);
        return amxd_status_unknown_error;
    }

    amxc_var_set_type(retval_map, AMXC_VAR_ID_HTABLE);

    amxc_var_add_key(uint16_t, retval_map, "Load", stats.load);
    amxc_var_add_key(int32_t, retval_map, "Noise", stats.noise);
    amxc_var_add_key(uint16_t, retval_map, "TxTime", stats.bss_transmit_time);
    amxc_var_add_key(uint16_t, retval_map, "RxTime", stats.bss_receive_time);
    amxc_var_add_key(uint16_t, retval_map, "IntTime", stats.other_bss_time + stats.other_time);
    amxc_var_add_key(uint16_t, retval_map, "ObssTime", stats.other_bss_time);
    amxc_var_add_key(uint16_t, retval_map, "NoiseTime", stats.other_time);
    amxc_var_add_key(uint16_t, retval_map, "FreeTime", stats.free_time);
    amxc_var_add_key(uint16_t, retval_map, "TotalTime", stats.total_time);
    amxc_var_add_key(uint32_t, retval_map, "Timestamp", stats.timestamp);
    amxc_var_add_key(uint8_t, retval_map, "ShortPreambleErrorPercentage", stats.short_preamble_error_percentage);
    amxc_var_add_key(uint8_t, retval_map, "LongPreambleErrorPercentage", stats.long_preamble_error_percentage);

    //Update parameters in data model
    amxd_object_t* radio = (amxd_object_t*) pR->pBus;
    amxd_trans_t trans;
    ASSERT_TRANSACTION_INIT(radio, &trans, amxd_status_unknown_error, ME, "%s : trans init failure", pR->Name);

    amxd_trans_set_uint16_t(&trans, "ChannelLoad", stats.load);
    amxd_trans_set_int32_t(&trans, "Noise", stats.noise);
    if(stats.total_time != 0) {
        amxd_trans_set_uint16_t(&trans, "Interference", (stats.other_bss_time * 100) / stats.total_time);
    } else {
        amxd_trans_set_uint16_t(&trans, "Interference", 0);
    }
    ASSERT_TRANSACTION_LOCAL_DM_END(&trans, amxd_status_unknown_error, ME, "%s : trans apply failure", pR->Name);


    amxc_var_add_key(amxc_htable_t, retval_map, "VendorStats", amxc_var_get_const_amxc_htable_t(stats.vendorStats));

    amxc_var_clean(&vendorStats);

    return amxd_status_ok;
}

amxd_status_t _getLatestPower(amxd_object_t* object,
                              amxd_function_t* func _UNUSED,
                              amxc_var_t* args _UNUSED,
                              amxc_var_t* retval) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = object->priv;

    if((pR->enable == 0) || (pR->status == RST_DOWN)) {
        SAH_TRACEZ_INFO(ME, "No power antenna when disabled");
        return amxd_status_unknown_error;
    }

    T_ANTENNA_POWER antenna_power;

    int ret = pR->pFA->mfn_wrad_latest_power(pR, &antenna_power);

    if(ret < 0) {
        SAH_TRACEZ_ERROR(ME, "Failed to get latest power info");
        return amxd_status_unknown_error;
    }

    amxc_var_set_type(retval, AMXC_VAR_ID_HTABLE);
    uint8_t i = 0;
    char value[16];

    for(i = 0; i < antenna_power.nr_antenna; i++) {
        snprintf(value, 16, "Antenna %u", i);
        amxc_var_add_key(double, retval, value, antenna_power.power_value[i]);
    }

    return amxd_status_ok;
}

/*
   Old and New rules... (( WIKI source )) In 2007 the FCC & ETSI began requiring that devices
   operating on 5.250–5.350 GHz and 5.470–5.725 GHz must employ dynamic frequency selection
   (DFS) and transmit power control (TPC) capabilities. This is to avoid interference with
   weather-radar and military applications. In 2010, the FCC further clarified the use of channels
   in the 5.470–5.725 GHz band to avoid interference with Terminal Doppler Weather Radar (TDWR)
   systems. These restrictions are now referred to collectively as the "Old Rules".

   On June 10, 2015, the FCC approved a new ruleset for 5 GHz device operation (called the "New
   Rules"), which adds 160 and 80 GHz channel identifiers, and re-enables previously prohibited
   DFS channels, in Publication Number 905462. This FCC publication eliminates the ability for
   manufacturers to have devices approved or modified under the Old Rules in phases.
   The New Rules apply in all circumstances as of June 2, 2016.[21]

   For this, we must check the drivers "channel list" in more detail to support also those extra
   channels with their supported channel bandwitdth when they become free.
   The extra functions introduced here are to simplify the task and they should be common for all
   our plugin versions!
 */
bool wld_rad_hasWeatherChannels(T_Radio* pRad) {
    for(int i = 0; pRad && pRad->possibleChannels[i]; i++) {
        if((pRad->possibleChannels[i] == 120) ||
           ( pRad->possibleChannels[i] == 124) ||
           ( pRad->possibleChannels[i] == 128)) {
            return TRUE;
        }
    }
    return FALSE;
}

bool wld_rad_hasChannel(T_Radio* pRad, int chan) {
    for(int i = 0; pRad && pRad->possibleChannels[i]; i++) {
        if(pRad->possibleChannels[i] == chan) {
            return TRUE;
        }
    }
    return FALSE;
}

bool wld_rad_hasChannelWidthCovered(T_Radio* pRad, swl_bandwidth_e chW) {
    return wld_channel_hasChannelWidthCovered(wld_rad_getSwlChanspec(pRad), chW);
}

wld_channel_extensionPos_e wld_rad_getExtensionChannel(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, WLD_CHANNEL_EXTENTION_POS_NONE, ME, "NULL");
    return wld_channel_getExtensionChannel(wld_rad_getSwlChanspec(pRad), pRad->extensionChannel);
}

bool wld_rad_hasEnabledEp(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    T_EndPoint* pEP = NULL;

    /* Check if NO EP is enabled */
    wld_rad_forEachEp(pEP, pRad) {
        if(pEP->enable) {
            return true;
        }
    }
    return false;
}

bool wld_rad_hasConnectedEp(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    T_EndPoint* pEP = NULL;

    /* Check if NO EP is connected */
    wld_rad_forEachEp(pEP, pRad) {
        if(pEP->connectionStatus == EPCS_CONNECTED) {
            return true;
        }
    }
    return false;
}

bool wld_rad_areAllVapsDone(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    T_AccessPoint* pAP = NULL;
    if(amxc_llist_size(&pRad->llAP) == 0) {
        return false;
    }

    /*
     * Check whether all configurable APs are initialized
     * (i.e conf loaded from saved/defaults , internal ctx synced with datamodel)
     */
    wld_rad_forEachAp(pAP, pRad) {
        if(!wld_vap_isDummyVap(pAP) && !pAP->initDone && !wld_ssid_isSSIDConfigured(pAP->pSSID)) {
            return false;
        }
    }
    return true;
}

bool wld_rad_hasEnabledVap(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    T_AccessPoint* pAP = NULL;

    /* Check if NO AP is enabled */
    wld_rad_forEachAp(pAP, pRad) {
        if(pAP->enable) {
            return true;
        }
    }
    return false;
}

bool wld_rad_hasActiveVap(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    T_AccessPoint* pAP = NULL;

    /* Check if NO AP is active */
    wld_rad_forEachAp(pAP, pRad) {
        if(pAP->status == APSTI_ENABLED) {
            return true;
        }
    }
    return false;
}

bool wld_rad_hasActiveEp(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    T_EndPoint* pEP = NULL;

    /* Check if NO AP is active */
    wld_rad_forEachEp(pEP, pRad) {
        if(pEP->status == APSTI_ENABLED) {
            return true;
        }
    }
    return false;
}

uint32_t wld_rad_getFirstEnabledIfaceIndex(T_Radio* pRad) {
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        if(wld_ssid_isLinkEnabled(pAP->pSSID)) {
            return wld_ssid_getLinkIfIndex(pAP->pSSID);
        }
    }
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pRad) {
        if(wld_ssid_isLinkEnabled(pEP->pSSID)) {
            return wld_ssid_getLinkIfIndex(pEP->pSSID);
        }
    }
    return 0;
}

bool wld_rad_hasEnabledIface(T_Radio* pRad) {
    return (wld_rad_getFirstEnabledIfaceIndex(pRad) > 0);
}

bool wld_rad_hasLinkIfIndex(T_Radio* pRad, int32_t ifIndex) {
    ASSERTS_NOT_NULL(pRad, false, ME, "NULL");
    ASSERTS_TRUE(ifIndex > 0, false, ME, "invalid");
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        if(wld_ssid_getLinkIfIndex(pAP->pSSID) == ifIndex) {
            return true;
        }
    }
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pRad) {
        if(wld_ssid_getLinkIfIndex(pEP->pSSID) == ifIndex) {
            return true;
        }
    }
    return (wld_getRadioByIndex(ifIndex) == pRad);
}

bool wld_rad_hasLinkIfName(T_Radio* pRad, const char* ifName) {
    ASSERTS_NOT_NULL(pRad, false, ME, "NULL");
    ASSERTS_STR(ifName, false, ME, "invalid");
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        if(swl_str_matches(wld_ssid_getLinkIfName(pAP->pSSID), ifName)) {
            return true;
        }
    }
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pRad) {
        if(swl_str_matches(wld_ssid_getLinkIfName(pEP->pSSID), ifName)) {
            return true;
        }
    }
    return (wld_rad_from_name(ifName) == pRad);
}

bool wld_rad_hasMloSupport(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, false, ME, "NULL");
    return ((wld_rad_checkEnabledRadStd(pRad, SWL_RADSTD_BE)) &&
            (pRad->pFA->mfn_misc_has_support(pRad, NULL, "MLO", 0) == true));
}

bool wld_rad_hasActiveApMld(T_Radio* pRad, uint32_t minNLinks) {
    ASSERTS_NOT_NULL(pRad, false, ME, "NULL");
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        if((pAP->pSSID != NULL) &&
           (wld_mld_isLinkActive(pAP->pSSID->pMldLink)) &&
           (wld_mld_countNeighActiveLinks(pAP->pSSID->pMldLink) >= minNLinks)) {
            return true;
        }
    }
    return false;
}

bool wld_rad_hasActiveApMldMultiLink(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, false, ME, "NULL");
    return wld_rad_hasActiveApMld(pRad, 2);
}

T_AccessPoint* wld_rad_getFirstActiveAp(T_Radio* pRad) {
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        if(wld_ssid_isLinkActive(pAP->pSSID)) {
            return pAP;
        }
    }
    return NULL;
}

T_AccessPoint* wld_rad_getFirstBroadcastingAp(T_Radio* pRad) {
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        if(wld_ssid_isLinkActive(pAP->pSSID) && pAP->SSIDAdvertisementEnabled) {
            return pAP;
        }
    }
    return NULL;
}

T_EndPoint* wld_rad_getFirstActiveEp(T_Radio* pRad) {
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pRad) {
        if(wld_ssid_isLinkActive(pEP->pSSID)) {
            return pEP;
        }
    }
    return NULL;
}

uint32_t wld_rad_getFirstActiveIfaceIndex(T_Radio* pRad) {
    ASSERTS_NOT_NULL(pRad, 0, ME, "NULL");
    T_AccessPoint* pAP = wld_rad_getFirstActiveAp(pRad);
    if(pAP != NULL) {
        return wld_ssid_getLinkIfIndex(pAP->pSSID);
    }
    T_EndPoint* pEP = wld_rad_getFirstActiveEp(pRad);
    if(pEP != NULL) {
        return wld_ssid_getLinkIfIndex(pEP->pSSID);
    }
    if((pRad->index > 0) && (wld_rad_isUpExt(pRad))) {
        return pRad->index;
    }
    return 0;
}

bool wld_rad_hasActiveIface(T_Radio* pRad) {
    return (wld_rad_getFirstActiveIfaceIndex(pRad) > 0);
}

uint32_t wld_rad_countVapIfaces(T_Radio* pRad) {
    uint32_t count = 0;
    T_AccessPoint* pAP;
    wld_rad_forEachAp(pAP, pRad) {
        count += (pAP->index > 0);
    }
    return count;
}

uint32_t wld_rad_countEpIfaces(T_Radio* pRad) {
    uint32_t count = 0;
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pRad) {
        count += (pEP->index > 0);
    }
    return count;
}

uint32_t wld_rad_countIfaces(T_Radio* pRad) {
    return (wld_rad_countVapIfaces(pRad) + wld_rad_countEpIfaces(pRad));
}

uint32_t wld_rad_countMappedAPs(T_Radio* pRad) {
    uint32_t count = 0;
    ASSERTS_NOT_NULL(pRad, count, ME, "NULL");
    amxd_object_t* ssidTmplObj = amxd_object_findf(get_wld_object(), "SSID");
    ASSERT_NOT_NULL(ssidTmplObj, count, ME, "Missing SSID template objs");
    amxd_object_t* apTmplObj = amxd_object_findf(get_wld_object(), "AccessPoint");
    ASSERT_NOT_NULL(apTmplObj, count, ME, "Missing AccessPoint template objs");
    amxd_object_for_each(instance, itSsid, ssidTmplObj) {
        amxd_object_t* ssidObj = amxc_container_of(itSsid, amxd_object_t, it);
        char* ssidLL = amxd_object_get_cstring_t(ssidObj, "LowerLayers", NULL);
        T_Radio* pRadOfSsid = (T_Radio*) swla_object_getReferenceObjectPriv(ssidObj, ssidLL);
        free(ssidLL);
        if(pRadOfSsid != pRad) {
            continue;
        }
        const char* ssidObjName = amxd_object_get_name(ssidObj, AMXD_OBJECT_NAMED);
        amxd_object_for_each(instance, itAp, apTmplObj) {
            amxd_object_t* apObj = amxc_container_of(itAp, amxd_object_t, it);
            const char* apObjName = amxd_object_get_name(apObj, AMXD_OBJECT_NAMED);
            bool matchSsid = (swl_str_matches(ssidObjName, apObjName));
            char* apSsidRef = amxd_object_get_cstring_t(apObj, "SSIDReference", NULL);
            if(!swl_str_isEmpty(apSsidRef)) {
                matchSsid = (swla_object_getReferenceObject(apObj, apSsidRef) == ssidObj);
            }
            free(apSsidRef);
            if(matchSsid) {
                SAH_TRACEZ_INFO(ME, "AP obj %s is mapped to Radio %s", apObjName, pRad->Name);
                count++;
                break;
            }
        }
    }
    SAH_TRACEZ_INFO(ME, "%d AP objs are mapped to Radio %s", count, pRad->Name);
    return count;
}

uint32_t wld_rad_countAPsByAutoMacSrc(T_Radio* pRad, wld_autoMacSrc_e autoMacSrc) {
    uint32_t count = 0;
    T_AccessPoint* pAP = NULL;
    wld_rad_forEachAp(pAP, pRad) {
        T_SSID* pSSID = pAP->pSSID;
        if((pSSID != NULL) && (pSSID->autoMacSrc == autoMacSrc)) {
            count++;
        }
    }
    return count;
}

uint32_t wld_rad_countWiphyRads(uint32_t wiphy) {
    uint32_t count = 0;
    T_Radio* pR;
    wld_for_eachRad(pR) {
        count += ((pR->wiphy == wiphy) && (pR->index > 0));
    }
    return count;
}

bool wld_rad_isChannelSubset(T_Radio* pRad, uint8_t* chanlist, int size) {
    int i = 0;
    int j = 0;

    for(i = 0; i < size; i++) {
        for(j = 0; j < pRad->nrPossibleChannels; j++) {
            if(pRad->possibleChannels[j] == chanlist[i]) {
                break;
            }
        }
        if(j == pRad->nrPossibleChannels) {
            return false;
        }
    }
    return true;
}

/**
 * Function to check if ONLY an active EndPoint is present.
 * In that case we can ignore some (AP) services as the Radio
 * initial act as a passive device.
 *
 * @param pRad - Raido handler
 *
 * @return bool - TRUE when no active AP are found on this Radio
 *         wow pure EP, else FALSE.
 */
bool wld_rad_hasOnlyActiveEP(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");

    T_EndPoint* pEP = NULL;
    /* Do we've an active EP? */
    amxc_llist_it_t* it = amxc_llist_get_first(&pRad->llEndPoints);
    if(it) {
        pEP = amxc_llist_it_get_data(it, T_EndPoint, it);
        if(!(pRad->isSTA && pRad->enable && pEP->enable)) {
            return false;
        }
    }

    return !wld_rad_hasActiveVap(pRad);
}

/*
 * @brief func checks if radio has endpoint as main interface
 */
bool wld_rad_hasMainEP(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    T_EndPoint* pEP;
    wld_rad_forEachEp(pEP, pRad) {
        if((pEP->index >= 0) && (pEP->index == pRad->index)) {
            return true;
        }
    }
    return false;
}

void wld_rad_updateChannelsInUse(T_Radio* pRad) {
    swl_channel_t chanInUse[SWL_BW_CHANNELS_MAX] = {0};
    uint8_t nbChanInUse = swl_chanspec_getChannelsInChanspec(&pRad->currentChanspec.chanspec, chanInUse, SWL_BW_CHANNELS_MAX);
    swl_typeUInt8_arrayToChar(pRad->channelsInUse, sizeof(pRad->channelsInUse), chanInUse, nbChanInUse);
}

static bool s_checkRadFreqBand(const T_Radio* pRad, swl_freqBandExt_e freqBand) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");

    if(pRad->operatingFrequencyBand == freqBand) {
        return true;
    }
    if((pRad->operatingFrequencyBand == SWL_FREQ_BAND_EXT_AUTO) && (SWL_BIT_IS_SET(pRad->supportedFrequencyBands, freqBand))) {
        swl_chanspec_t chSpec = SWL_CHANSPEC_NEW(pRad->channel, SWL_BW_AUTO, freqBand);
        if((swl_chanspec_channelFromMHz(&chSpec, swl_chanspec_channelToMHzDef(&chSpec, 0)) >= SWL_RC_OK) &&
           (chSpec.band == freqBand)) {
            return true;
        }
    }

    return false;
}

bool wld_rad_is_6ghz(const T_Radio* pRad) {
    return s_checkRadFreqBand(pRad, SWL_FREQ_BAND_EXT_6GHZ);
}

bool wld_rad_is_5ghz(const T_Radio* pRad) {
    return s_checkRadFreqBand(pRad, SWL_FREQ_BAND_EXT_5GHZ);
}

bool wld_rad_is_24ghz(const T_Radio* pRad) {
    return s_checkRadFreqBand(pRad, SWL_FREQ_BAND_EXT_2_4GHZ);
}

bool wld_rad_is_on_dfs(T_Radio* pRad) {
    if(pRad == NULL) {
        SAH_TRACEZ_ERROR(ME, "req dfs of null rad");
        return false;
    }
    return wld_channel_is_dfs_band(pRad->channel, swl_radBw_toBw[pRad->runningChannelBandwidth]);
}

bool wld_rad_isDoingDfsScan(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    return ((pRad->detailedState == CM_RAD_FG_CAC)
            || (pRad->detailedState == CM_RAD_BG_CAC)
            || (pRad->detailedState == CM_RAD_BG_CAC_NS)
            || (pRad->bgdfs_config.channel != 0));
}

/**
 * Return whether the radio is up and not doing any scanning itself.
 * External scanning is allowed.
 */
bool wld_rad_isUpAndReady(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    return ((pRad->detailedState == CM_RAD_UP)
            || (pRad->detailedState == CM_RAD_BG_CAC_EXT)
            || (pRad->detailedState == CM_RAD_BG_CAC_EXT_NS)
            || (pRad->detailedState == CM_RAD_DELAY_AP_UP))
           && !wld_scan_isRunning(pRad);
}


/**
 * Return whether the radio is up
 * Need to retrieve current operating status of the radio based on driver
 * status to avoid any undesirable states due to configuration process which
 * could not be reflected in radio global status
 */
bool wld_rad_isUpExt(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, false, ME, "NULL");
    return ((pRad->detailedState == CM_RAD_UP)
            || (pRad->detailedState == CM_RAD_FG_CAC)
            || (pRad->detailedState == CM_RAD_BG_CAC)
            || (pRad->detailedState == CM_RAD_BG_CAC_NS)
            || (pRad->detailedState == CM_RAD_BG_CAC_EXT)
            || (pRad->detailedState == CM_RAD_BG_CAC_EXT_NS)
            || (pRad->detailedState == CM_RAD_DELAY_AP_UP));
}

void wld_rad_write_possible_channels(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, , ME, "NULL");
    ASSERTI_TRUE(pRad->hasDmReady, , ME, "%s: radio dm obj not ready for updates", pRad->Name);
    char TBuf[320] = {'\0'};
    swl_conv_uint8ArrayToChar(TBuf, sizeof(TBuf), pRad->possibleChannels, pRad->nrPossibleChannels);

    SAH_TRACEZ_INFO(ME, "%s: saving possible channels (%s)", pRad->Name, TBuf);
    amxd_trans_t trans;
    ASSERT_TRANSACTION_INIT(pRad->pBus, &trans, , ME, "%s : trans init failure", pRad->Name);
    amxd_trans_set_value(cstring_t, &trans, "PossibleChannels", TBuf);
    amxd_trans_set_value(cstring_t, &trans, "MaxChannelBandwidth", Rad_SupBW[pRad->maxChannelBandwidth]);
    ASSERT_TRANSACTION_LOCAL_DM_END(&trans, , ME, "%s : trans apply failure", pRad->Name);
}

bool wld_rad_has_endpoint_enabled(T_Radio* rad) {
    return wld_rad_getEnabledEndpoint(rad) != NULL;
}

T_EndPoint* wld_rad_getEnabledEndpoint(T_Radio* rad) {
    ASSERT_NOT_NULL(rad, NULL, ME, "NULL");
    T_EndPoint* pEndpoint;
    wld_rad_forEachEp(pEndpoint, rad) {
        if(pEndpoint && pEndpoint->enable) {
            return pEndpoint;
        }
    }
    return NULL;
}

bool wld_rad_hasRunningEndpoint(T_Radio* rad) {
    return wld_rad_getRunningEndpoint(rad) != NULL;
}

T_EndPoint* wld_rad_getRunningEndpoint(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, NULL, ME, "NULL");
    T_EndPoint* pEP = NULL;

    /* Check if NO EP is connecting/connected with saved profile or wps session */
    wld_rad_forEachEp(pEP, pRad) {
        if((pEP->index > 0) && (pRad->pFA->mfn_wendpoint_status(pEP) >= SWL_RC_OK) &&
           ((pEP->wpsSessionInfo.WPS_PairingInProgress) ||
            (pEP->connectionStatus == EPCS_DISCOVERING) ||
            (pEP->connectionStatus == EPCS_CONNECTING) ||
            (pEP->connectionStatus == EPCS_CONNECTED))) {
            return pEP;
        }
    }
    return NULL;
}

bool wld_rad_hasWpsActiveEndpoint(T_Radio* rad) {
    return wld_rad_getWpsActiveEndpoint(rad) != NULL;
}

T_EndPoint* wld_rad_getWpsActiveEndpoint(T_Radio* rad) {
    ASSERT_NOT_NULL(rad, NULL, ME, "NULL");
    T_EndPoint* pEndpoint;
    wld_rad_forEachEp(pEndpoint, rad) {
        if(pEndpoint && (pEndpoint->connectionStatus == EPCS_WPS_PAIRING)) {
            return pEndpoint;
        }
    }
    return NULL;
}

void wld_rad_updateActiveDevices(T_Radio* pRad, amxd_trans_t* trans) {
    ASSERT_NOT_NULL(pRad, , ME, "NULL");
    T_AccessPoint* pAP;

    int nrActiveDevices = 0;
    uint32_t nrActiveVideoDevices = 0;

    wld_rad_forEachAp(pAP, pRad) {
        nrActiveDevices += pAP->ActiveAssociatedDeviceNumberOfEntries;
        nrActiveVideoDevices += wld_ad_get_nb_active_video_stations(pAP);
    }
    pRad->currentStations = nrActiveDevices;
    pRad->currentVideoStations = nrActiveVideoDevices;

    amxd_trans_select_object(trans, pRad->pBus);

    amxd_trans_set_int32_t(trans, "ActiveAssociatedDevices", pRad->currentStations);
    amxd_trans_set_uint32_t(trans, "ActiveVideoAssociatedDevices", pRad->currentVideoStations);
}

T_Radio* wld_rad_get_radio(const char* ifname) {
    T_Radio* pRad = NULL;
    wld_for_eachRad(pRad) {
        if(pRad && !strcmp(pRad->Name, ifname)) {
            return pRad;
        }
    }

    return NULL;
}

void wld_rad_chan_update_model(T_Radio* pRad, amxd_trans_t* trans) {

    swla_trans_t tmpTrans;
    amxd_trans_t* targetTrans = swla_trans_init(&tmpTrans, trans, pRad->pBus);
    ASSERT_NOT_NULL(targetTrans, , ME, "NULL");

    /* Update object fields directly if changed */
    amxd_trans_set_uint32_t(targetTrans, "Channel", pRad->channel);
    amxd_trans_set_cstring_t(targetTrans, "ChannelsInUse", pRad->channelsInUse);

    char operatingStandardsText[64] = {};
    swl_radStd_toChar(operatingStandardsText, sizeof(operatingStandardsText), pRad->operatingStandards, pRad->operatingStandardsFormat, pRad->supportedStandards);
    amxd_trans_set_cstring_t(targetTrans, "OperatingStandards", operatingStandardsText);
    /* Do not update OperatingChannelBandwidth dm value when syncing with chanspec read from driver
     * to not be considered as a user config by the OperatingChannelBandwidth dm writer hanlder
     */
    if((pRad->targetChanspec.chanspec.channel != 0) && ((pRad->channelBandwidthChangeReason != CHAN_REASON_INITIAL) || (pRad->operatingChannelBandwidth != pRad->runningChannelBandwidth))) {
        amxd_trans_set_cstring_t(targetTrans, "OperatingChannelBandwidth", swl_radBw_str[pRad->operatingChannelBandwidth]);
    }
    amxd_trans_set_cstring_t(targetTrans, "CurrentOperatingChannelBandwidth", swl_radBw_str[pRad->runningChannelBandwidth]);
    swl_conv_transParamSetMask(targetTrans, "SupportedOperatingChannelBandwidth", pRad->supportedChannelBandwidth,
                               swl_radBw_str, SWL_RAD_BW_MAX);

    amxd_trans_set_cstring_t(targetTrans, "ChannelChangeReason", g_wld_channelChangeReason_str[pRad->channelChangeReason]);
    amxd_trans_set_cstring_t(targetTrans, "ChannelBandwidthChangeReason", g_wld_channelChangeReason_str[pRad->channelBandwidthChangeReason]);

    swla_trans_finalize(&tmpTrans, NULL);
}

void wld_rad_updateOperatingClass(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, , ME, "NULL");
    ASSERTI_TRUE(pRad->hasDmReady, , ME, "%s: radio dm obj not ready for updates", pRad->Name);
    swl_chanspec_t chanspec = wld_rad_getSwlChanspec(pRad);
    swl_opClassCountry_e countryZone = getCountryZone(pRad->regulatoryDomainIdx);
    pRad->operatingClass = swl_chanspec_getLocalOperClass(&chanspec, countryZone);
    if(pRad->operatingClass == 0) {
        pRad->operatingClass = swl_chanspec_getOperClass(&chanspec);
        SAH_TRACEZ_WARNING(ME, "%s: fall back to global operClass %d for chanspec %s",
                           pRad->Name, pRad->operatingClass, swl_typeChanspecExt_toBuf32(chanspec).buf);
    }

    ASSERT_TRUE(swl_typeUInt32_commitObjectParam(pRad->pBus, "OperatingClass", pRad->operatingClass), ,
                ME, "%s: fail to commit operating class (%d)", pRad->Name, pRad->operatingClass);
    SAH_TRACEZ_INFO(ME, "%s: set operatingClass to %d", pRad->Name, pRad->operatingClass);
}

void _wld_rad_setOperatingClass(const char* const sig_name _UNUSED,
                                const amxc_var_t* const data,
                                void* const priv _UNUSED) {
    amxd_object_t* object = amxd_dm_signal_get_object(get_wld_plugin_dm(), data);
    ASSERTS_NOT_NULL(object, , ME, "NULL");
    T_Radio* pRad = (T_Radio*) object->priv;
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    ASSERTS_NOT_NULL(pRad->pBus, , ME, "NULL");
    wld_rad_updateOperatingClass(pRad);
}

void _wld_rad_updatePossibleChannels_ocf(const char* const sig_name _UNUSED,
                                         const amxc_var_t* const data,
                                         void* const priv _UNUSED) {
    amxd_object_t* object = amxd_dm_signal_get_object(get_wld_plugin_dm(), data);
    ASSERTS_NOT_NULL(object, , ME, "NULL");
    T_Radio* pRad = (T_Radio*) object->priv;
    ASSERTS_NOT_NULL(pRad, , ME, "NULL");
    ASSERTS_NOT_NULL(pRad->pBus, , ME, "NULL");
    pRad->pFA->mfn_wrad_poschans(pRad, NULL, 0);
}

void s_selectRadio(amxd_object_t* const object, int32_t depth _UNUSED, void* priv) {
    ASSERTS_EQUALS(amxd_object_get_type(object), amxd_object_instance, , ME, "Not instance");
    amxd_object_t* parent = amxd_object_get_parent(object);
    ASSERTS_TRUE(swl_str_matches(amxd_object_get_name(parent, AMXD_OBJECT_NAMED), "Radio"), , ME, "Not under Radio");
    ASSERTS_EQUALS(amxd_object_get_parent(parent), get_wld_object(), , ME, "Not under WiFi");
    T_Radio** ppRad = (T_Radio**) priv;
    ASSERTS_NOT_NULL(ppRad, , ME, "NULL");
    *ppRad = object->priv;
}
void _wld_rad_setVendorData_ocf(const char* const sig_name _UNUSED,
                                const amxc_var_t* const data,
                                void* const priv _UNUSED) {
    amxd_dm_t* dm = get_wld_plugin_dm();
    amxd_object_t* object = amxd_dm_signal_get_object(dm, data);
    T_Radio* pR = NULL;
    amxd_object_hierarchy_walk(object, amxd_direction_up, NULL, s_selectRadio, INT32_MAX, &pR);
    ASSERT_TRUE(debugIsRadPointer(pR), , ME, "NO radio Ctx");
    SAH_TRACEZ_INFO(ME, "%s: %s rad vendor data (%s)", pR->Name, sig_name, GET_CHAR(data, "object"));
    pR->pFA->mfn_wifi_supvend_modes(pR, NULL, object, GET_ARG(data, "parameters"));
}

void wld_rad_init_counters(T_Radio* pRad, T_EventCounterList* counters, const char** defaults) {
    amxd_object_t* counters_template = amxd_object_findf(pRad->pBus, "EventCounter");

    amxd_trans_t trans;
    ASSERT_TRANSACTION_INIT(counters_template, &trans, , ME, "%s : trans init failure", pRad->Name);

    for(uint32_t i = 0; i < counters->nrCounters; i++) {
        amxd_trans_select_object(&trans, counters_template);
        amxd_trans_add_inst(&trans, 0, counters->names[i]);

        amxd_trans_set_cstring_t(&trans, "Key", counters->names[i]);
        if((defaults != NULL) && (defaults[i] != NULL)) {
            amxd_trans_set_cstring_t(&trans, "Info", defaults[i]);
        }
    }

    ASSERT_TRANSACTION_LOCAL_DM_END(&trans, , ME, "%s : trans apply failure", pRad->Name);

    for(uint32_t i = 0; i < counters->nrCounters; i++) {
        counters->values[i].object = amxd_object_get_instance(counters_template, counters->names[i], 0);
    }

}

void wld_rad_increment_counter(T_Radio* pRad, T_EventCounterList* counters, uint32_t index, const char* info) {
    (void) pRad;
    ASSERTI_TRUE(index < counters->nrCounters, , ME, "VALUE");
    T_EventCounter* value = &(counters->values[index]);
    value->counter++;

    char* oldInfo = amxd_object_get_cstring_t(value->object, "Info", NULL);
    SAH_TRACEZ_WARNING(ME, "EVENT %s: %s : %u : %s (was %s)", pRad->Name, counters->names[index], value->counter, info, oldInfo);
    free(oldInfo);

    value->lastEventTime = swl_time_getMonoSec();

    amxd_trans_t trans;
    ASSERT_TRANSACTION_INIT(value->object, &trans, , ME, "%s : trans init failure", pRad->Name);

    amxd_trans_set_uint32_t(&trans, "Value", value->counter);
    swl_typeTimeMono_toTransParam(&trans, "LastOccurrence", value->lastEventTime);
    amxd_trans_set_cstring_t(&trans, "Info", info);

    ASSERT_TRANSACTION_LOCAL_DM_END(&trans, , ME, "%s : trans apply failure", pRad->Name);
}

void wld_rad_incrementCounterStr(T_Radio* pRad, T_EventCounterList* counters, uint32_t index, const char* template, ...) {
    va_list args;
    va_start(args, template);

    char buffer[250];
    vsnprintf(buffer, sizeof(buffer), template, args);
    va_end(args);
    wld_rad_increment_counter(pRad, counters, index, buffer);
}

T_AssociatedDevice* wld_rad_getAssociatedDevice(T_Radio* pRad, swl_macBin_t* macBin) {
    ASSERTS_NOT_NULL(pRad, NULL, ME, "NULL");
    ASSERTS_NOT_NULL(macBin, NULL, ME, "NULL");
    amxc_llist_for_each(it, &pRad->llAP) {
        T_AccessPoint* pAP = amxc_llist_it_get_data(it, T_AccessPoint, it);
        T_AssociatedDevice* pAD = wld_vap_get_existing_station(pAP, (swl_macBin_t*) macBin);
        if(pAD != NULL) {
            return pAD;
        }
    }
    return NULL;
}

/**
 * Whether radio is available for commands
 */
bool wld_rad_isAvailable(T_Radio* pRad) {
    if((pRad->status == RST_ERROR)
       || (pRad->status == RST_UNKNOWN)
       || (pRad->status == RST_NOTPRESENT)
       || (pRad->detailedState == CM_RAD_DEEP_POWER_DOWN)) {
        return false;
    }
    return true;
}

T_AccessPoint* wld_radio_getVapFromRole(T_Radio* pRad, wld_apRole_e role) {
    ASSERTS_NOT_NULL(pRad, NULL, ME, "NULL");
    amxc_llist_for_each(it, &pRad->llAP) {
        T_AccessPoint* pAP = amxc_llist_it_get_data(it, T_AccessPoint, it);
        if(pAP->apRole == role) {
            return pAP;
        }
    }

    return NULL;
}

T_AccessPoint* wld_rad_firstAp(T_Radio* pRad) {
    ASSERT_TRUE(debugIsRadPointer(pRad), NULL, ME, "NULL");
    amxc_llist_it_t* it = amxc_llist_get_first(&pRad->llAP);
    ASSERTS_NOT_NULL(it, NULL, ME, "Empty");
    T_AccessPoint* pAP = wld_ap_fromIt(it);
    ASSERTS_TRUE(debugIsVapPointer(pAP), NULL, ME, "%s: invalid first vap", pRad->Name);
    return pAP;
}
T_AccessPoint* wld_rad_nextAp(T_Radio* pRad _UNUSED, T_AccessPoint* pAP) {
    ASSERT_NOT_NULL(pAP, NULL, ME, "NULL");
    amxc_llist_it_t* it = amxc_llist_it_get_next(&pAP->it);
    ASSERTS_NOT_NULL(it, NULL, ME, "Last");
    T_AccessPoint* nextAp = wld_ap_fromIt(it);
    ASSERTS_TRUE(debugIsVapPointer(nextAp), NULL, ME, "%s: invalid next vap of %s", pRad->Name, pAP->name);
    return nextAp;
}

T_EndPoint* wld_rad_firstEp(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, NULL, ME, "NULL");
    amxc_llist_it_t* it = amxc_llist_get_first(&pRad->llEndPoints);
    ASSERTS_NOT_NULL(it, NULL, ME, "Empty");
    T_EndPoint* pEP = wld_endpoint_fromIt(it);
    ASSERTS_TRUE(debugIsEpPointer(pEP), NULL, ME, "%s: invalid first ep", pRad->Name);
    return pEP;
}

T_EndPoint* wld_rad_nextEp(T_Radio* pRad _UNUSED, T_EndPoint* pEP) {
    ASSERT_NOT_NULL(pEP, NULL, ME, "NULL");
    amxc_llist_it_t* it = amxc_llist_it_get_next(&pEP->it);
    ASSERTS_NOT_NULL(it, NULL, ME, "Last");
    T_EndPoint* nextEp = wld_endpoint_fromIt(it);
    ASSERTS_TRUE(debugIsEpPointer(nextEp), NULL, ME, "%s: invalid next ep", pRad->Name, pEP->Name);
    return nextEp;
}

T_AccessPoint* wld_rad_getVapByIndex(T_Radio* pRad, int index) {
    ASSERT_NOT_NULL(pRad, NULL, ME, "NULL");
    T_AccessPoint* pAP = NULL;
    wld_rad_forEachAp(pAP, pRad) {
        if(pAP->index == index) {
            return pAP;
        }
    }
    return NULL;
}

T_EndPoint* wld_rad_getEpByIndex(T_Radio* pRad, int index) {
    ASSERT_NOT_NULL(pRad, NULL, ME, "NULL");
    T_EndPoint* pEP = NULL;
    wld_rad_forEachEp(pEP, pRad) {
        if(pEP->index == index) {
            return pEP;
        }
    }
    return NULL;
}

T_Radio* wld_rad_fromIt(amxc_llist_it_t* it) {
    if(it == NULL) {
        return NULL;
    }
    return amxc_llist_it_get_data(it, T_Radio, it);
}

/**
 * Radio is enabled, and likely available for commands.
 */
bool wld_rad_isActive(T_Radio* pRad) {
    return pRad->status == RST_UP || pRad->status == RST_DORMANT;
}

/**
 * Radio is up and broadcasting signal.
 */
bool wld_rad_isUp(T_Radio* pRad) {
    return pRad->status == RST_UP;
}

/* Set the socket on the Radio structure. */
int wld_rad_getSocket(T_Radio* rad) {
    ASSERT_NOT_NULL(rad, -1, ME, "NULL");
    ASSERT_TRUE(rad->wlRadio_SK > 0, -1, ME, "INVALID");

    return rad->wlRadio_SK;
}

void wld_rad_resetStatusHistogram(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, , ME, "NULL");
    memset(&pRad->changeInfo.statusHistogram.data, 0, sizeof(pRad->changeInfo.statusHistogram.data));
}

swl_freqBand_e wld_rad_getFreqBand(T_Radio* pRad) {
    return swl_chanspec_freqBandExtToFreqBand(pRad->operatingFrequencyBand, SWL_FREQ_BAND_2_4GHZ, NULL);
}

void wld_rad_updateState(T_Radio* pRad, bool forceVapUpdate) {
    ASSERTI_NOT_NULL(pRad, , ME, "Radio null");
    if(pRad->detailedState >= CM_RAD_MAX) {
        SAH_TRACEZ_ERROR(ME, "Detailed rad state wrong %s : %u, entering error", pRad->Name, pRad->detailedState);
        pRad->detailedState = CM_RAD_ERROR;
    }
    wld_util_updateStatusChangeInfo(&pRad->changeInfo, pRad->status);

    int curState = pRad->pFA->mfn_wrad_radio_status(pRad);
    wld_status_e oldStatus = pRad->status;

    if((pRad->enable && wld_util_areAllVapsDisabled(pRad) && wld_util_areAllEndpointsDisabled(pRad)) || (pRad->detailedState == CM_RAD_FG_CAC)) {
        pRad->status = RST_DORMANT;
    } else if((pRad->detailedState == CM_RAD_DOWN) || (curState == 0) || (pRad->detailedState == CM_RAD_DEEP_POWER_DOWN)) {
        pRad->status = RST_DOWN;
    } else if(pRad->detailedState == CM_RAD_UNKNOWN) {
        pRad->status = RST_UNKNOWN;
    } else if(pRad->detailedState == CM_RAD_ERROR) {
        pRad->status = RST_ERROR;
    } else {
        // In case of BG_CAC or plain UP -> just set up
        pRad->status = RST_UP;
    }

    //prepare radio status change event to all subscribers
    wld_radio_status_change_event_t event;
    event.radio = pRad;
    event.oldStatus = oldStatus;
    event.oldDetailedState = pRad->detailedStatePrev;

    //Update object
    if(pRad->detailedState != pRad->detailedStatePrev) {
        pRad->detailedStatePrev = pRad->detailedState;
        amxd_object_t* chanObject = amxd_object_get_child(pRad->pBus, "ChannelMgt");
        if((chanObject != NULL) && (pRad->hasDmReady)) {
            ASSERT_TRUE(swl_typeCharPtr_commitObjectParam(chanObject, "RadioStatus", (char*) cstr_chanmgt_rad_state[pRad->detailedState]), ,
                        ME, "%s: fail to commit channelMgt radioStatus", pRad->Name);
        }

    }

    if((event.oldDetailedState != pRad->detailedState) || (event.oldStatus != pRad->status)) {
        //send radio status change event to all subscribers
        wld_event_trigger_callback(gWld_queue_rad_onStatusChange, &event);
        SAH_TRACEZ_INFO(ME, "%s update status from %u/%u to %u/%u at %s", pRad->Name,
                        event.oldStatus, event.oldDetailedState,
                        pRad->status, pRad->detailedState,
                        swl_typeTimeMono_toBuf32(pRad->changeInfo.lastStatusChange).buf);
    }

    if(oldStatus != pRad->status) {
        SAH_TRACEZ_WARNING(ME, "%s update %s(%u) -> %s(%u)",
                           pRad->Name,
                           wld_status_str[oldStatus], oldStatus,
                           wld_status_str[pRad->status], pRad->status);

        pRad->changeInfo.lastStatusChange = swl_time_getMonoSec();
        pRad->changeInfo.nrStatusChanges++;

        if((pRad->pBus != NULL) && (pRad->hasDmReady)) {
            amxd_trans_t trans;
            ASSERT_TRANSACTION_INIT(pRad->pBus, &trans, , ME, "%s : trans init failure", pRad->Name);
            amxd_trans_set_value(cstring_t, &trans, "Status", Rad_SupStatus[pRad->status]);
            swl_typeTimeMono_toTransParam(&trans, "LastStatusChangeTimeStamp", pRad->changeInfo.lastStatusChange);
            ASSERT_TRANSACTION_LOCAL_DM_END(&trans, , ME, "%s : trans apply failure", pRad->Name);
        }
    } else {
        SAH_TRACEZ_INFO(ME, "%s: check state same update %s(%u) ", pRad->Name, wld_status_str[pRad->status], pRad->status);

    }

    if((oldStatus == pRad->status) && !forceVapUpdate) {
        SAH_TRACEZ_INFO(ME, "%s : Same state %u / %u", pRad->Name, pRad->status, pRad->detailedState);
        return;
    }

    if((pRad->status == RST_DOWN) && pRad->obssCoexistenceActive) {
        SAH_TRACEZ_INFO(ME, "%s : disabling obssCoexistence", pRad->Name);
        pRad->obssCoexistenceActive = false;
    }

    //Update VAPs

    T_AccessPoint* pAP = NULL;

    wld_rad_forEachAp(pAP, pRad) {
        wld_vap_updateState(pAP);
    }

    wld_radStaMon_updateActive(pRad);
}

void wld_rad_triggerChangeEvent(T_Radio* pRad, wld_rad_changeEvent_e event, void* data) {
    wld_rad_changeEvent_t changeEv;
    changeEv.pRad = pRad;
    changeEv.changeType = event;
    changeEv.changeData = data;

    wld_event_trigger_callback(gWld_queue_rad_onChangeEvent, &changeEv);
}

void wld_rad_triggerFrameEvent(T_Radio* pRad, swl_bit8_t* frame, size_t frameLen, int32_t rssi) {
    wld_rad_frameEvent_t frameEv;
    frameEv.pRad = pRad;
    frameEv.frameData = frame;
    frameEv.frameLen = frameLen;
    frameEv.frameRssi = rssi;

    wld_event_trigger_callback(gWld_queue_rad_onFrameEvent, &frameEv);
}

uint32_t wld_rad_getCurrentFreq(T_Radio* pRad) {
    ASSERT_NOT_NULL(pRad, 0, ME, "NULL");
    swl_chanspec_t spec = wld_rad_getSwlChanspec(pRad);
    return swl_chanspec_toMHzDef(&spec, 0);
}

static void s_setStats(amxc_var_t* pRetMap, T_Stats* pStats) {
    amxc_var_add_key(uint64_t, pRetMap, "BytesSent", pStats->BytesSent);
    amxc_var_add_key(uint64_t, pRetMap, "BytesSent", pStats->BytesSent);
    amxc_var_add_key(uint64_t, pRetMap, "BytesReceived", pStats->BytesReceived);
    amxc_var_add_key(uint64_t, pRetMap, "PacketsSent", pStats->PacketsSent);
    amxc_var_add_key(uint64_t, pRetMap, "PacketsReceived", pStats->PacketsReceived);
    amxc_var_add_key(uint32_t, pRetMap, "ErrorsSent", pStats->ErrorsSent);
    amxc_var_add_key(uint32_t, pRetMap, "ErrorsReceived", pStats->ErrorsReceived);
    amxc_var_add_key(uint32_t, pRetMap, "RetransCount", pStats->RetransCount);
    amxc_var_add_key(uint32_t, pRetMap, "DiscardPacketsSent", pStats->DiscardPacketsSent);
    amxc_var_add_key(uint32_t, pRetMap, "DiscardPacketsReceived", pStats->DiscardPacketsReceived);
    amxc_var_add_key(uint32_t, pRetMap, "UnicastPacketsSent", pStats->UnicastPacketsSent);
    amxc_var_add_key(uint32_t, pRetMap, "UnicastPacketsReceived", pStats->UnicastPacketsReceived);
    amxc_var_add_key(uint32_t, pRetMap, "MulticastPacketsSent", pStats->MulticastPacketsSent);
    amxc_var_add_key(uint32_t, pRetMap, "MulticastPacketsReceived", pStats->MulticastPacketsReceived);
    amxc_var_add_key(uint32_t, pRetMap, "BroadcastPacketsSent", pStats->BroadcastPacketsSent);
    amxc_var_add_key(uint32_t, pRetMap, "BroadcastPacketsReceived", pStats->BroadcastPacketsReceived);
    amxc_var_add_key(uint32_t, pRetMap, "UnknownProtoPacketsReceived", pStats->UnknownProtoPacketsReceived);
    amxc_var_add_key(uint32_t, pRetMap, "FailedRetransCount", pStats->FailedRetransCount);
    amxc_var_add_key(uint32_t, pRetMap, "RetryCount", pStats->RetryCount);
    amxc_var_add_key(uint32_t, pRetMap, "MultipleRetryCount", pStats->MultipleRetryCount);
}

amxd_object_t* wld_rad_getObject(T_Radio* pRad) {
    return pRad->pBus;
}

bool wld_rad_firstCommitFinished(T_Radio* pRad) {
    if(pRad->counterList[WLD_RAD_EV_FSM_COMMIT].counter == 0) {
        return false;
    }
    if(pRad->counterList[WLD_RAD_EV_FSM_COMMIT].counter > 1) {
        return true;
    }
    return pRad->fsmRad.FSM_State == FSM_IDLE;
}

swl_rc_ne wld_rad_registerExtModData(T_Radio* pRad, uint32_t extModId, void* extModData, wld_extMod_deleteData_dcf deleteHandler) {
    ASSERT_NOT_NULL(pRad, SWL_RC_INVALID_PARAM, ME, "NULL");
    return wld_extMod_registerData(&pRad->extDataList, extModId, extModData, deleteHandler);
}

void* wld_rad_getExtModData(T_Radio* pRad, uint32_t extModId) {
    ASSERT_NOT_NULL(pRad, NULL, ME, "NULL");
    return wld_extMod_getData(&pRad->extDataList, extModId);
}

swl_rc_ne wld_rad_unregisterExtModData(T_Radio* pRad, uint32_t extModId) {
    ASSERT_NOT_NULL(pRad, SWL_RC_INVALID_PARAM, ME, "NULL");
    return wld_extMod_unregisterData(&pRad->extDataList, extModId);
}


amxd_status_t _Radio_getStatusHistogram(amxd_object_t* object,
                                        amxd_function_t* func _UNUSED,
                                        amxc_var_t* args _UNUSED,
                                        amxc_var_t* retval) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pRad = object->priv;
    amxc_var_init(retval);
    amxc_var_set_type(retval, AMXC_VAR_ID_HTABLE);
    wld_util_updateStatusChangeInfo(&pRad->changeInfo, pRad->status);
    swl_type_toVariant((swl_type_t*) &gtWld_status_changeInfo, retval, &pRad->changeInfo);

    SAH_TRACEZ_OUT(ME);
    return amxd_status_ok;
}

amxd_status_t _getMaxTransmitPowerdBm(amxd_object_t* object,
                                      amxd_function_t* func _UNUSED,
                                      amxc_var_t* args,
                                      amxc_var_t* retval) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, amxd_status_unknown_error, ME, "no radio mapped");
    uint16_t channel = amxc_var_dyncast(uint16_t, GET_ARG(args, "channel"));
    ASSERT_TRUE(wld_rad_hasChannel(pR, channel), amxd_status_invalid_arg, ME, "%s : invalid chan %d", pR->Name, channel);

    int32_t dbm;
    int rc = pR->pFA->mfn_wrad_getMaxTxPow_dBm(pR, channel, &dbm);
    if(rc != SWL_RC_OK) {
        SAH_TRACEZ_ERROR(ME, "Failed to get max txpower");
        return amxd_status_unknown_error;
    }

    amxc_var_set(int32_t, retval, dbm);
    SAH_TRACEZ_OUT(ME);
    return amxd_status_ok;
}

amxd_status_t _getCurrentTransmitPowerdBm(amxd_object_t* object,
                                          amxd_function_t* func _UNUSED,
                                          amxc_var_t* args _UNUSED,
                                          amxc_var_t* retval) {

    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(object);
    ASSERTS_NOT_NULL(pR, amxd_status_unknown_error, ME, "no radio mapped");
    ASSERTI_TRUE(wld_rad_isActive(pR), amxd_status_unknown_error, ME, "%s : not ready", pR->Name);

    int32_t dbm;
    swl_rc_ne rc = pR->pFA->mfn_wrad_getCurrentTxPow_dBm(pR, &dbm);
    if(rc != SWL_RC_OK) {
        SAH_TRACEZ_ERROR(ME, "Failed to get current txpower");
        return amxd_status_unknown_error;
    }

    amxc_var_set(int32_t, retval, dbm);
    SAH_TRACEZ_OUT(ME);
    return amxd_status_ok;
}

static void s_dumpRadioDebug(T_Radio* pR, amxc_var_t* retval) {
    amxc_var_add_key(bool, retval, "Enable", pR->enable);
    amxc_var_add_key(cstring_t, retval, "Status", wld_status_str[pR->status]);
    swl_type_addToMap(&gtWld_status_changeInfo.type.type, retval, "changeInfo", &pR->changeInfo);
    amxc_var_add_key(int32_t, retval, "Index", pR->index);
    amxc_var_add_key(uint64_t, retval, "WDevId", pR->wDevId);
    amxc_var_add_key(int32_t, retval, "RefIndex", pR->ref_index);
    amxc_var_add_key(bool, retval, "IsReady", pR->isReady);
    swl_conv_addMaskToMap(retval, "SupportedFrequencyBands", pR->supportedFrequencyBands, swl_freqBandExt_str, SWL_FREQ_BAND_EXT_MAX);
    amxc_var_add_key(cstring_t, retval, "OperatingFrequencyBand", swl_freqBandExt_str[pR->operatingFrequencyBand]);
    swl_conv_addMaskToMap(retval, "SupportedStandards", pR->supportedStandards, swl_radStd_str, SWL_RADSTD_MAX);
    swl_conv_addMaskToMap(retval, "OperatingStandards", pR->operatingStandards, swl_radStd_str, SWL_RADSTD_MAX);
    amxc_var_add_key(cstring_t, retval, "OperatingChannelBandwidth", swl_radBw_str[pR->operatingChannelBandwidth]);
}

amxd_status_t _Radio_debug(amxd_object_t* object,
                           amxd_function_t* func _UNUSED,
                           amxc_var_t* args,
                           amxc_var_t* retval) {

    T_Radio* pR = object->priv;

    const char* feature = GET_CHAR(args, "op");
    ASSERT_NOT_NULL(feature, amxd_status_unknown_error, ME, "No argument given");

    amxc_var_init(retval);
    amxc_var_set_type(retval, AMXC_VAR_ID_HTABLE);

    if(!strcasecmp(feature, "RssiMon")) {
        wld_radStaMon_debug(pR, retval);
    } else if(!strcasecmp(feature, "DriverConfig")) {
        amxc_var_add_key(int32_t, retval, "TxBurst", pR->driverCfg.txBurst);
        amxc_var_add_key(int32_t, retval, "Amsdu", pR->driverCfg.amsdu);
        amxc_var_add_key(int32_t, retval, "Ampdu", pR->driverCfg.ampdu);
        amxc_var_add_key(int32_t, retval, "FragThreshold", pR->driverCfg.fragThreshold);
        amxc_var_add_key(int32_t, retval, "RtsThreshold", pR->driverCfg.rtsThreshold);
        amxc_var_add_key(int32_t, retval, "TxBeamforming", pR->driverCfg.txBeamforming);
        amxc_var_add_key(bool, retval, "VhtOmnEnabled", pR->driverCfg.vhtOmnEnabled);
        amxc_var_add_key(int32_t, retval, "BroadcastMaxBwCapability", pR->driverCfg.broadcastMaxBwCapability);
        amxc_var_add_key(uint32_t, retval, "TPCMode", pR->driverCfg.tpcMode);
    } else if(!strcasecmp(feature, "MacConfig")) {
        amxc_var_add_key(bool, retval, "UseBaseMacOffset", pR->macCfg.useBaseMacOffset);
        amxc_var_add_key(bool, retval, "UseLocalBitForGuest", pR->macCfg.useLocalBitForGuest);
        amxc_var_add_key(int64_t, retval, "BaseMacOffset", pR->macCfg.baseMacOffset);
        amxc_var_add_key(int64_t, retval, "LocalGuestMacOffset", pR->macCfg.localGuestMacOffset);
        amxc_var_add_key(uint32_t, retval, "NrBssRequired", pR->macCfg.nrBssRequired);
    } else if(!strcasecmp(feature, "DelayCommit")) {
        if(pR->pFA->mfn_wrad_fsm_delay_commit) {
            pR->pFA->mfn_wrad_fsm_delay_commit(pR);
        } else {
            amxc_var_add_key(cstring_t, retval, "Error", "No delay commit function");
        }
    } else if(!strcasecmp(feature, "listFeatures")) {
        s_listRadioFeatures(pR, retval);
    } else if(!strcasecmp(feature, "hapdCfg")) {
        char tmpName[128];
        snprintf(tmpName, sizeof(tmpName), "%s-%s.tmp.txt", "/tmp/hostapd", pR->Name);
        wld_hostapd_cfgFile_create(pR, tmpName);
    } else if(!strcasecmp(feature, "nl80211IfaceInfo")) {
        wld_nl80211_ifaceInfo_t ifaceInfo;
        if(wld_rad_nl80211_getInterfaceInfo(pR, &ifaceInfo) < SWL_RC_OK) {
            amxc_var_add_key(cstring_t, retval, "Error", "Fail to get nl80211 iface info");
        } else {
            wld_nl80211_dumpIfaceInfo(&ifaceInfo, retval);
        }
    } else if(!strcasecmp(feature, "nl80211IfacesInfo")) {
        uint32_t nrWiphyMax = GET_UINT32(args, "wiphyMax");
        nrWiphyMax = (nrWiphyMax > 0) ? nrWiphyMax : MAXNROF_RADIO;
        wld_nl80211_ifaceInfo_t wlIfacesInfo[nrWiphyMax][MAXNROF_ACCESSPOINT];
        memset(wlIfacesInfo, 0, sizeof(wlIfacesInfo));
        if(wld_nl80211_getInterfaces(nrWiphyMax, MAXNROF_ACCESSPOINT, wlIfacesInfo) < SWL_RC_OK) {
            amxc_var_add_key(cstring_t, retval, "Error", "Fail to get nl80211 ifaces info");
        } else {
            for(uint32_t i = 0; i < nrWiphyMax; i++) {
                wld_nl80211_ifaceInfo_t* pMainIface = &wlIfacesInfo[i][0];
                if(pMainIface->ifIndex <= 0) {
                    continue;
                }
                wld_nl80211_dumpIfaceInfo(pMainIface, amxc_var_add_key(amxc_htable_t, retval, pMainIface->name, NULL));
            }
        }
    } else if(!strcasecmp(feature, "nl80211WiphyInfo")) {
        wld_nl80211_wiphyInfo_t wiphyInfo;
        if(wld_rad_nl80211_getWiphyInfo(pR, &wiphyInfo) < SWL_RC_OK) {
            amxc_var_add_key(cstring_t, retval, "Error", "Fail to get nl80211 wiphy info");
        } else {
            wld_nl80211_dumpWiphyInfo(&wiphyInfo, retval);
        }
    } else if(!strcasecmp(feature, "nl80211AllWiphyInfo")) {
        uint32_t nrWiphy;
        wld_nl80211_wiphyInfo_t wiphyInfo[MAXNROF_RADIO];
        if((wld_nl80211_getAllWiphyInfo(wld_nl80211_getSharedState(), MAXNROF_RADIO, wiphyInfo, &nrWiphy) < SWL_RC_OK) ||
           (!nrWiphy)) {
            amxc_var_add_key(cstring_t, retval, "Error", "Fail to get nl80211 all wiphy info");
        } else {
            for(uint32_t i = 0; i < nrWiphy; i++) {
                wld_nl80211_dumpWiphyInfo(&wiphyInfo[i], amxc_var_add_key(amxc_htable_t, retval, wiphyInfo[i].name, NULL));
            }
        }
    } else if(swl_str_matchesIgnoreCase(feature, "RadioLinuxStats")) {

        T_Stats radioStats;
        memset(&radioStats, 0, sizeof(radioStats));

        if(wld_linuxIfStats_getRadioStats(pR, &radioStats)) {
            s_setStats(retval, &radioStats);
        }
    } else if(swl_str_matchesIgnoreCase(feature, "AllEpLinuxStats")) {

        T_Stats epStats;
        memset(&epStats, 0, sizeof(epStats));

        if(wld_linuxIfStats_getAllEpStats(pR, &epStats)) {
            s_setStats(retval, &epStats);
        }
    } else if(swl_str_matchesIgnoreCase(feature, "AllVapLinuxStats")) {

        T_Stats vapStats;
        memset(&vapStats, 0, sizeof(vapStats));

        if(wld_linuxIfStats_getAllVapStats(pR, &vapStats)) {
            s_setStats(retval, &vapStats);
        }
    } else if(swl_str_matchesIgnoreCase(feature, "nl80211SetAntennas")) {
        uint32_t txMapAnt = GET_UINT32(args, "txMapAnt");
        uint32_t rxMapAnt = GET_UINT32(args, "rxMapAnt");
        swl_rc_ne ret = wld_rad_nl80211_setAntennas(pR, txMapAnt, rxMapAnt);
        amxc_var_add_key(cstring_t, retval, "Result", swl_rc_toString(ret));
    } else if(swl_str_matchesIgnoreCase(feature, "nl80211SetTxPowerAuto")) {
        swl_rc_ne ret = wld_rad_nl80211_setTxPowerAuto(pR);
        amxc_var_add_key(cstring_t, retval, "Result", swl_rc_toString(ret));
    } else if(swl_str_matchesIgnoreCase(feature, "nl80211SetTxPowerFixed")) {
        int32_t mbm = GET_INT32(args, "mbm");
        swl_rc_ne ret = wld_rad_nl80211_setTxPowerFixed(pR, mbm);
        amxc_var_add_key(cstring_t, retval, "Result", swl_rc_toString(ret));
    } else if(swl_str_matchesIgnoreCase(feature, "nl80211SetTxPowerLimited")) {
        int32_t mbm = GET_INT32(args, "mbm");
        swl_rc_ne ret = wld_rad_nl80211_setTxPowerLimited(pR, mbm);
        amxc_var_add_key(cstring_t, retval, "Result", swl_rc_toString(ret));
    } else if(swl_str_matchesIgnoreCase(feature, "nl80211GetTxPower")) {
        int32_t dbm;
        swl_rc_ne ret = wld_rad_nl80211_getTxPower(pR, &dbm);
        amxc_var_add_key(int32_t, retval, "txPwr", dbm);
        amxc_var_add_key(cstring_t, retval, "Result", swl_rc_toString(ret));
    } else if(swl_str_matchesIgnoreCase(feature, "getFreq")) {
        amxc_var_add_key(uint32_t, retval, "freq", wld_rad_getCurrentFreq(pR));
    } else if(swl_str_matchesIgnoreCase(feature, "FSM")) {
        amxc_var_add_key(uint32_t, retval, "fsmState", pR->fsmRad.FSM_State);
        amxc_var_add_key(uint32_t, retval, "fsmReqState", pR->fsmRad.FSM_ReqState);
        amxc_var_add_key(int32_t, retval, "fsmRetry", pR->fsmRad.FSM_Retry);
        amxc_var_add_key(int32_t, retval, "fsmComPend", pR->fsmRad.FSM_ComPend);
        amxc_var_add_key(int32_t, retval, "fsmDelay", pR->fsmRad.FSM_Delay);
        amxc_var_add_key(int32_t, retval, "fsmLoop", pR->fsmRad.FSM_Loop);
        amxc_var_add_key(uint32_t, retval, "timeout", pR->fsmRad.timeout_msec);
        amxc_var_add_key(bool, retval, "timerPresent", pR->fsmRad.timer != NULL);
        amxc_var_add_key(uint32_t, retval, "timeRemaining", amxp_timer_remaining_time(pR->fsmRad.timer));
        amxc_var_add_key(uint32_t, retval, "retryCount", pR->fsmRad.retryCount);
        amxc_var_add_key(bool, retval, "externalLock", wld_rad_fsm_doesExternalLocking(pR));
        char buffer[128] = {0};
        snprintf(buffer, sizeof(buffer), "0x%08lx - 0x%08lx", pR->fsmRad.FSM_BitActionArray[0], pR->fsmRad.FSM_BitActionArray[1]);
        amxc_var_add_key(cstring_t, retval, "FSM_BitArray", buffer);
        snprintf(buffer, sizeof(buffer), "0x%08lx - 0x%08lx", pR->fsmRad.FSM_AC_BitActionArray[0], pR->fsmRad.FSM_AC_BitActionArray[1]);
        amxc_var_add_key(cstring_t, retval, "FSM_AC_BitArray", buffer);
        amxc_var_add_key(uint32_t, retval, "nrStart", pR->fsmStats.nrStarts);
        amxc_var_add_key(uint32_t, retval, "nrRunStarts", pR->fsmStats.nrRunStarts);
        amxc_var_add_key(uint32_t, retval, "nrFinish", pR->fsmStats.nrFinish);
    } else if(swl_str_matchesIgnoreCase(feature, "getTxPwrPct")) {
        int32_t txPwrPct = -1;
        swl_rc_ne ret = s_getTxPowerPct(pR, &txPwrPct);
        amxc_var_add_key(int32_t, retval, "txPwrPct", txPwrPct);
        amxc_var_add_key(cstring_t, retval, "Result", swl_rc_toString(ret));
    } else if(swl_str_matchesIgnoreCase(feature, "printChannels")) {
        wld_channel_print(pR);
    } else if(swl_str_matchesIgnoreCase(feature, "dump")) {
        s_dumpRadioDebug(pR, retval);
    } else {
        amxc_var_add_key(cstring_t, retval, "Error", "Unknown command");
    }
    return amxd_status_ok;
}

SWLA_DM_HDLRS(sRadioDmHdlrs,
              ARR(SWLA_DM_PARAM_HDLR("RegulatoryDomain", s_setCountryCode_pwf),
                  SWLA_DM_PARAM_HDLR("IEEE80211hEnabled", s_set80211hEnable_pwf),
                  SWLA_DM_PARAM_HDLR("AP_Mode", s_setAPMode_pwf),
                  SWLA_DM_PARAM_HDLR("STA_Mode", s_setSTAMode_pwf),
                  SWLA_DM_PARAM_HDLR("WDS_Mode", s_setWDSMode_pwf),
                  SWLA_DM_PARAM_HDLR("WET_Mode", s_setWETMode_pwf),
                  SWLA_DM_PARAM_HDLR("STASupported_Mode", s_setStaSupMode_pwf),
                  SWLA_DM_PARAM_HDLR("WPS_Enrollee_Mode", s_setWPSEnrolMode_pwf),
                  SWLA_DM_PARAM_HDLR("KickRoamingStation", s_setKickRoamSta_pwf),
                  SWLA_DM_PARAM_HDLR("ExternalAcsMgmt", s_setExternalAcsMgmt_pwf),
                  SWLA_DM_PARAM_HDLR("AutoChannelEnable", s_setAutoChannelEnable_pwf),
                  SWLA_DM_PARAM_HDLR("AutoBandwidthSelectMode", s_setAutoBandwidthSelectMode_pwf),
                  SWLA_DM_PARAM_HDLR("OperatingChannelBandwidth", s_setOperatingChannelBandwidth_pwf),
                  SWLA_DM_PARAM_HDLR("Channel", s_setChannel_pwf),
                  SWLA_DM_PARAM_HDLR("ObssCoexistenceEnable", s_setObssCoexistenceEnable_pwf),
                  SWLA_DM_PARAM_HDLR("ExtensionChannel", s_setExtensionChannel_pwf),
                  SWLA_DM_PARAM_HDLR("GuardInterval", s_setGuardInterval_pwf),
                  SWLA_DM_PARAM_HDLR("TransmitPower", s_setTxPower_pwf),
                  SWLA_DM_PARAM_HDLR("ActiveAntennaCtrl", s_setAntennaCtrl_pwf),
                  SWLA_DM_PARAM_HDLR("RxChainCtrl", s_setRxChainCtrl_pwf),
                  SWLA_DM_PARAM_HDLR("TxChainCtrl", s_setTxChainCtrl_pwf),
                  SWLA_DM_PARAM_HDLR("RetryLimit", s_setRetryLimit_pwf),
                  SWLA_DM_PARAM_HDLR("RTSThreshold", s_setRtsThreshold_pwf),
                  SWLA_DM_PARAM_HDLR("LongRetryLimit", s_setLongRetryLimit_pwf),
                  SWLA_DM_PARAM_HDLR("BeaconPeriod", s_setBeaconPeriod_pwf),
                  SWLA_DM_PARAM_HDLR("DTIMPeriod", s_setDtimPeriod_pwf),
                  SWLA_DM_PARAM_HDLR("PreambleType", s_setPreambleType_pwf),
                  SWLA_DM_PARAM_HDLR("PacketAggregationEnable", s_setPacketAggregationEnable_pwf),
                  SWLA_DM_PARAM_HDLR("TargetWakeTimeEnable", s_setTargetWakeTimeEnable_pwf),
                  SWLA_DM_PARAM_HDLR("OfdmaEnable", s_setOfdmaEnable_pwf),
                  SWLA_DM_PARAM_HDLR("HeCapsEnabled", s_setHeCaps_pwf),
                  SWLA_DM_PARAM_HDLR("ImplicitBeamFormingEnabled", s_setImplicitBeamForming_pwf),
                  SWLA_DM_PARAM_HDLR("ExplicitBeamFormingEnabled", s_setExplicitBeamForming_pwf),
                  SWLA_DM_PARAM_HDLR("RxBeamformingCapsEnabled", s_setRxBfCapsEnabled_pwf),
                  SWLA_DM_PARAM_HDLR("TxBeamformingCapsEnabled", s_setTxBfCapsEnabled_pwf),
                  SWLA_DM_PARAM_HDLR("RIFSEnabled", s_setRIFS_pwf),
                  SWLA_DM_PARAM_HDLR("AirtimeFairnessEnabled", s_setAirTimeFairness_pwf),
                  SWLA_DM_PARAM_HDLR("RxPowerSaveEnabled", s_setRxPowerSave_pwf),
                  SWLA_DM_PARAM_HDLR("RxPowerSaveRepeaterEnable", s_setRxPowerSaveRepeater_pwf),
                  SWLA_DM_PARAM_HDLR("MultiUserMIMOEnabled", s_setMultiUserMIMO_pwf),
                  SWLA_DM_PARAM_HDLR("MaxAssociatedDevices", s_setMaxStations_pwf),
                  SWLA_DM_PARAM_HDLR("IntelligentAirtimeSchedulingEnable", s_setIntelligentAirtime_pwf),
                  SWLA_DM_PARAM_HDLR("dbgRADEnable", s_setDbgEnable_pwf),
                  SWLA_DM_PARAM_HDLR("dbgRADFile", s_setDbgFile_pwf),
                  SWLA_DM_PARAM_HDLR("OperatingStandards", wld_rad_setOperatingStandards_pwf),
                  SWLA_DM_PARAM_HDLR("OperatingStandardsFormat", wld_rad_setOperatingStandardsFormat_pwf),
                  SWLA_DM_PARAM_HDLR("DelayApUpPeriod", wld_rad_delayMgr_setDelayApUpPeriod_pwf),
                  SWLA_DM_PARAM_HDLR("Enable", s_setEnable_pwf),
                  SWLA_DM_PARAM_HDLR("OperationalDataTransmitRates", s_setOperationalDataTransmitRates_pwf),
                  SWLA_DM_PARAM_HDLR("BasicDataTransmitRates", s_setBasicDataTransmitRates_pwf),
                  ),
              .instAddedCb = s_addInstance_oaf);

void _wld_radio_setConf_ocf(const char* const sig_name,
                            const amxc_var_t* const data,
                            void* const priv) {
    swla_dm_procObjEvtOfLocalDm(&sRadioDmHdlrs, sig_name, data, priv);
}

static void s_setBssColor_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "NULL");

    uint8_t bssColor = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set Bss Color %d", pR->Name, bssColor);
    pR->cfg11ax.heBssColor = bssColor;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setBssColorPartial_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t bssColorPartial = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set BSS color AID equation %d", pR->Name, bssColorPartial);
    pR->cfg11ax.heBssColorPartial = bssColorPartial;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setHESIGASpatialReuseValue15Allowed_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t srValue15Allowed = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set HESIGA Spatial Reuse value 15 allowed %d", pR->Name, srValue15Allowed);
    pR->cfg11ax.heHESIGASpatialReuseValue15Allowed = srValue15Allowed;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setSRGInformationValid_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t srSRGInformationValid = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set SRG Information Valid value %d", pR->Name, srSRGInformationValid);
    pR->cfg11ax.heSRGInformationValid = srSRGInformationValid;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setNonSRGOffsetValid_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t srNonSRGOffsetValid = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set Non SRG Offset Valid value %d", pR->Name, srNonSRGOffsetValid);
    pR->cfg11ax.heNonSRGOffsetValid = srNonSRGOffsetValid;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setPSRDisallowed_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t srPSRDisallowed = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set PSR Disallowed value %d", pR->Name, srPSRDisallowed);
    pR->cfg11ax.hePSRDisallowed = srPSRDisallowed;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setNonSRGOBSSPDMaxOffset_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t nonSrgObssPdMaxOffset = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set Non-SRG OBSS PD Max Offset value %d", pR->Name, nonSrgObssPdMaxOffset);
    pR->cfg11ax.heSprNonSrgObssPdMaxOffset = nonSrgObssPdMaxOffset;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setSRGOBSSPDMinOffset_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t srgObssPdMinOffset = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set SRG OBSS PD Min Offset value %d", pR->Name, srgObssPdMinOffset);
    pR->cfg11ax.heSprSrgObssPdMinOffset = srgObssPdMinOffset;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setSRGOBSSPDMaxOffset_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    uint8_t srgObssPdMaxOffset = amxc_var_dyncast(uint8_t, newValue);
    SAH_TRACEZ_INFO(ME, "%s: set SRG OBSS PD Max Offset value %d", pR->Name, srgObssPdMaxOffset);
    pR->cfg11ax.heSprSrgObssPdMaxOffset = srgObssPdMaxOffset;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

static void s_setSRGBSSColorBitmap_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    char* bssColorStr = amxc_var_dyncast(cstring_t, newValue);
    ASSERT_NOT_NULL(bssColorStr, , ME, "NULL");
    SAH_TRACEZ_INFO(ME, "%s: set BSS Colors Bitmap %s", pR->Name, bssColorStr);
    if(!swl_str_matches(pR->cfg11ax.heSprSrgBssColors, bssColorStr)) {
        swl_str_copy(pR->cfg11ax.heSprSrgBssColors, HE_SPR_SRG_BSS_COLORS_MAX_LEN, bssColorStr);
        wld_rad_doSync(pR);
    }
    free(bssColorStr);

    SAH_TRACEZ_OUT(ME);
}

static void s_setSRGPartialBSSIDBitmap_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "INVALID");

    char* srgPartialBssid = amxc_var_dyncast(cstring_t, newValue);
    ASSERT_NOT_NULL(srgPartialBssid, , ME, "NULL");
    SAH_TRACEZ_INFO(ME, "%s: set SPR SRG Partial BSSID Bitmap %s", pR->Name, srgPartialBssid);
    if(!swl_str_matches(pR->cfg11ax.heSprSrgPartialBssid, srgPartialBssid)) {
        swl_str_copy(pR->cfg11ax.heSprSrgPartialBssid, HE_SPR_SRG_PARTIAL_BSSID_MAX_LEN, srgPartialBssid);
        wld_rad_doSync(pR);
    }
    free(srgPartialBssid);

    SAH_TRACEZ_OUT(ME);
}

static void s_setMBSSIDAdvertisementMode_pwf(void* priv _UNUSED, amxd_object_t* object, amxd_param_t* param _UNUSED, const amxc_var_t* const newValue) {
    SAH_TRACEZ_IN(ME);

    T_Radio* pR = wld_rad_fromObj(amxd_object_get_parent(object));
    ASSERT_NOT_NULL(pR, , ME, "NULL");
    const char* mbssidModeStr = amxc_var_constcast(cstring_t, newValue);
    ASSERT_NOT_NULL(mbssidModeStr, , ME, "NULL");
    wld_mbssidAdvertisement_mode_e newMbssidMode = swl_conv_charToEnum(mbssidModeStr, g_wld_mbssidAdvertisementMode, MBSSID_ADVERTISEMENT_MODE_MAX, MBSSID_ADVERTISEMENT_MODE_OFF);
    ASSERTS_NOT_EQUALS(newMbssidMode, pR->mbssidAdsMode, , ME, "EQUALS");

    pR->mbssidAdsMode = newMbssidMode;
    wld_rad_doSync(pR);

    SAH_TRACEZ_OUT(ME);
}

SWLA_DM_HDLRS(sRadio11axDmHdlrs,
              ARR(SWLA_DM_PARAM_HDLR("BssColor", s_setBssColor_pwf),
                  SWLA_DM_PARAM_HDLR("BssColorPartial", s_setBssColorPartial_pwf),
                  SWLA_DM_PARAM_HDLR("HESIGASpatialReuseValue15Allowed", s_setHESIGASpatialReuseValue15Allowed_pwf),
                  SWLA_DM_PARAM_HDLR("SRGInformationValid", s_setSRGInformationValid_pwf),
                  SWLA_DM_PARAM_HDLR("NonSRGOffsetValid", s_setNonSRGOffsetValid_pwf),
                  SWLA_DM_PARAM_HDLR("PSRDisallowed", s_setPSRDisallowed_pwf),
                  SWLA_DM_PARAM_HDLR("NonSRGOBSSPDMaxOffset", s_setNonSRGOBSSPDMaxOffset_pwf),
                  SWLA_DM_PARAM_HDLR("SRGOBSSPDMinOffset", s_setSRGOBSSPDMinOffset_pwf),
                  SWLA_DM_PARAM_HDLR("SRGOBSSPDMaxOffset", s_setSRGOBSSPDMaxOffset_pwf),
                  SWLA_DM_PARAM_HDLR("SRGBSSColorBitmap", s_setSRGBSSColorBitmap_pwf),
                  SWLA_DM_PARAM_HDLR("SRGPartialBSSIDBitmap", s_setSRGPartialBSSIDBitmap_pwf),
                  SWLA_DM_PARAM_HDLR("MBSSIDAdvertisementMode", s_setMBSSIDAdvertisementMode_pwf),
                  ));

void _wld_rad_11ax_setConf_ocf(const char* const sig_name,
                               const amxc_var_t* const data,
                               void* const priv) {
    swla_dm_procObjEvtOfLocalDm(&sRadio11axDmHdlrs, sig_name, data, priv);
}


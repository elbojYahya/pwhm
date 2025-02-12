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
 * This file implements nl80211 api (requests)
 */

#include "wld_nl80211_api_priv.h"
#include "wld_nl80211_api.h"
#include "wld_nl80211_parser.h"
#include "wld_nl80211_utils.h"
#include "wld_linuxIfUtils.h"
#include "dirent.h"

#include "swl/swl_common.h"
#include "swl/swl_assert.h"
#include "swl/swl_hex.h"
#include "swla/swla_mac.h"
#include "swl/swl_genericFrameParser.h"

#define ME "nlApi"
#define NL80211_WLD_VENDOR_NAME "nl80211"
#define SYSFS_IEEE80211_PATH "/sys/class/ieee80211/"

const T_CWLD_FUNC_TABLE* wld_nl80211_getVendorTable() {
    vendor_t* pVendor = wld_getVendorByName(NL80211_WLD_VENDOR_NAME);
    ASSERTS_NOT_NULL(pVendor, NULL, ME, "NULL");
    return &pVendor->fta;
}

const wld_fsmMngr_t* wld_nl80211_getFsmMngr() {
    vendor_t* pVendor = wld_getVendorByName(NL80211_WLD_VENDOR_NAME);
    ASSERTS_NOT_NULL(pVendor, NULL, ME, "NULL");
    return pVendor->fsmMngr;
}

vendor_t* wld_nl80211_registerVendor(T_CWLD_FUNC_TABLE* fta) {
    return wld_registerVendor(NL80211_WLD_VENDOR_NAME, fta);
}

struct getWirelessIfacesData_s {
    const uint32_t nrIfacesMax;
    uint32_t nrIfaces;
    wld_nl80211_ifaceInfo_t* pIfaces;
};
static swl_rc_ne s_getInterfaceInfoCb(swl_rc_ne rc, struct nlmsghdr* nlh, void* priv) {
    ASSERTS_FALSE((rc <= SWL_RC_ERROR), rc, ME, "Request error");
    ASSERT_NOT_NULL(nlh, SWL_RC_ERROR, ME, "NULL");
    ASSERTI_EQUALS(nlh->nlmsg_type, g_nl80211DriverIDs.family_id, SWL_RC_OK, ME, "skip msgtype %d", nlh->nlmsg_type);
    struct genlmsghdr* gnlh = (struct genlmsghdr*) nlmsg_data(nlh);
    ASSERTI_EQUALS(gnlh->cmd, NL80211_CMD_NEW_INTERFACE, SWL_RC_OK, ME, "unexpected cmd %d", gnlh->cmd);
    struct getWirelessIfacesData_s* requestData = (struct getWirelessIfacesData_s*) priv;
    ASSERT_NOT_NULL(requestData, SWL_RC_ERROR, ME, "No request data");
    // Parse the netlink message
    struct nlattr* tb[NL80211_ATTR_MAX + 1] = {};
    if(nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL)) {
        SAH_TRACEZ_ERROR(ME, "Failed to parse netlink message");
        return SWL_RC_ERROR;
    }
    wld_nl80211_ifaceInfo_t ifaceInfo;
    rc = wld_nl80211_parseInterfaceInfo(tb, &ifaceInfo);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "parsing failed");
    ASSERT_NOT_EQUALS(ifaceInfo.wiphy, WLD_NL80211_ID_UNDEF, SWL_RC_ERROR, ME, "missing wiphy");
    ASSERT_NOT_EQUALS(ifaceInfo.ifIndex, WLD_NL80211_ID_UNDEF, SWL_RC_ERROR, ME, "missing ifIndex");
    ASSERT_NOT_EQUALS(ifaceInfo.ifIndex, 0, SWL_RC_ERROR, ME, "Invalid net dev index");
    ASSERT_NOT_EQUALS(ifaceInfo.name[0], 0, SWL_RC_ERROR, ME, "missing interface name");

    if((requestData->pIfaces == NULL) || (requestData->nrIfacesMax == 0)) {
        SAH_TRACEZ_INFO(ME, "interface %s skipped: no storage available", ifaceInfo.name);
        return SWL_RC_OK;
    }
    if(requestData->nrIfaces >= requestData->nrIfacesMax) {
        SAH_TRACEZ_INFO(ME, "interface %s skipped: maxIfaces %d reached", ifaceInfo.name, requestData->nrIfacesMax);
        return SWL_RC_DONE;
    }

    wld_nl80211_ifaceInfo_t* pIface = &requestData->pIfaces[requestData->nrIfaces];
    memcpy(pIface, &ifaceInfo, sizeof(*pIface));
    requestData->nrIfaces++;
    return SWL_RC_OK;
}

static int s_ifaceInfoCmp(const void* e1, const void* e2) {
    wld_nl80211_ifaceInfo_t* pIface1 = (wld_nl80211_ifaceInfo_t*) e1;
    wld_nl80211_ifaceInfo_t* pIface2 = (wld_nl80211_ifaceInfo_t*) e2;
    if(pIface1->wiphy == pIface2->wiphy) {
        return pIface1->ifIndex - pIface2->ifIndex;
    }
    return (pIface1->wiphy - pIface2->wiphy);
}
swl_rc_ne wld_nl80211_getInterfacesList(wld_nl80211_state_t* state, wld_nl80211_ifaceInfo_t** pWlIfaces, uint32_t maxWlIfaces, uint32_t* pNrWlIfaces) {
    ASSERT_NOT_NULL(pWlIfaces, SWL_RC_INVALID_PARAM, ME, "NULL");
    ASSERT_NOT_NULL(pNrWlIfaces, SWL_RC_INVALID_PARAM, ME, "NULL");
    int32_t maxIfaces = (int32_t) maxWlIfaces ? : wld_nl80211_countWiphyFromFS() * 32;
    ASSERT_TRUE(maxIfaces > 0, SWL_RC_ERROR, ME, "null target size");
    struct getWirelessIfacesData_s requestData = {
        .nrIfacesMax = maxIfaces,
        .nrIfaces = 0,
        .pIfaces = calloc(maxIfaces, sizeof(wld_nl80211_ifaceInfo_t)),
    };
    swl_rc_ne rc = wld_nl80211_sendCmdSync(state,
                                           NL80211_CMD_GET_INTERFACE, NLM_F_DUMP,
                                           0, NULL, s_getInterfaceInfoCb, &requestData);
    //sort wlifaces by wiphy then ifIndex, to get ordered vaps per radio
    qsort(requestData.pIfaces, requestData.nrIfaces, sizeof(wld_nl80211_ifaceInfo_t), s_ifaceInfoCmp);
    W_SWL_SETPTR(pNrWlIfaces, requestData.nrIfaces);
    W_SWL_SETPTR(pWlIfaces, requestData.pIfaces);
    return rc;
}

swl_rc_ne wld_nl80211_getInterfaces(const uint32_t nrWiphyMax, const uint32_t nrWifaceMax,
                                    wld_nl80211_ifaceInfo_t wlIfaces[nrWiphyMax][nrWifaceMax]) {
    size_t maxIfaces = nrWiphyMax * nrWifaceMax;
    struct getWirelessIfacesData_s requestData = {
        .nrIfacesMax = maxIfaces,
        .nrIfaces = 0,
        .pIfaces = NULL,
    };
    memset(wlIfaces, 0, maxIfaces * sizeof(wld_nl80211_ifaceInfo_t));
    swl_rc_ne rc = wld_nl80211_getInterfacesList(wld_nl80211_getSharedState(), &requestData.pIfaces, requestData.nrIfacesMax, &requestData.nrIfaces);
    wld_nl80211_ifaceInfo_t* pWlIface;
    uint32_t lastWiphy = WLD_NL80211_ID_UNDEF;
    uint32_t curWiphyPos = 0;
    uint32_t curIfacePos = 0;
    for(uint32_t i = 0; i < requestData.nrIfaces; i++) {
        if((curWiphyPos >= nrWiphyMax) || (curIfacePos >= nrWifaceMax)) {
            break;
        }
        if(lastWiphy != requestData.pIfaces[i].wiphy) {
            if(curIfacePos > 0) {
                curWiphyPos++;
                curIfacePos = 0;
            }
            if(curWiphyPos >= nrWiphyMax) {
                break;
            }
            lastWiphy = requestData.pIfaces[i].wiphy;
        }
        pWlIface = &wlIfaces[curWiphyPos][curIfacePos];
        memcpy(pWlIface, &requestData.pIfaces[i], sizeof(*pWlIface));
        SAH_TRACEZ_INFO(ME, "Wireless[%d][%d] iface (name:%s,index:%d) %s %s%s",
                        curWiphyPos, curIfacePos,
                        pWlIface->name, pWlIface->ifIndex,
                        (pWlIface->isMain ? "main" : "virtual"),
                        (pWlIface->isAp ? "AP" : ""),
                        (pWlIface->isSta ? "EP" : ""));
        curIfacePos++;
    }
    free(requestData.pIfaces);
    return rc;
}

swl_rc_ne wld_nl80211_getInterfaceInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_ifaceInfo_t* pIfaceInfo) {
    ASSERT_TRUE(ifIndex > 0, SWL_RC_INVALID_PARAM, ME, "null ifIndex");
    struct getWirelessIfacesData_s requestData = {
        .nrIfacesMax = 1,
        .nrIfaces = 0,
        .pIfaces = calloc(1, sizeof(wld_nl80211_ifaceInfo_t)),
    };
    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_GET_INTERFACE, 0,
                                           ifIndex, NULL, s_getInterfaceInfoCb, &requestData);
    if(requestData.nrIfaces == 0) {
        SAH_TRACEZ_ERROR(ME, "no AP/Station interface with ifIndex(%d)", ifIndex);
        rc = SWL_RC_ERROR;
    } else if(pIfaceInfo) {
        memcpy(pIfaceInfo, requestData.pIfaces, sizeof(wld_nl80211_ifaceInfo_t));
    }
    free(requestData.pIfaces);
    return rc;
}

swl_rc_ne wld_nl80211_newInterface(wld_nl80211_state_t* state, uint32_t ifIndex, const char* ifName,
                                   const swl_macBin_t* pMac, bool isAp, bool isSta,
                                   wld_nl80211_ifaceInfo_t* pIfaceInfo) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_TRUE((isAp || isSta), rc, ME, "invalid type");

    wld_nl80211_newIfaceConf_t ifaceConf;
    memset(&ifaceConf, 0, sizeof(ifaceConf));

    //no nl80211 interface type for mixed apsta mode
    ifaceConf.type = (isAp ? NL80211_IFTYPE_AP : NL80211_IFTYPE_STATION);
    if(pMac != NULL) {
        memcpy(ifaceConf.mac.bMac, pMac->bMac, SWL_MAC_BIN_LEN);
    }

    return wld_nl80211_newInterfaceExt(state, ifIndex, ifName, &ifaceConf, pIfaceInfo);
}

static swl_rc_ne s_newInterfaceExt(wld_nl80211_state_t* state, uint32_t ifIndex, uint32_t wiphyId, const char* ifName,
                                   wld_nl80211_newIfaceConf_t* pIfaceConf,
                                   wld_nl80211_ifaceInfo_t* pIfaceInfo) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_NOT_NULL(ifName, rc, ME, "NULL");
    ASSERT_NOT_EQUALS(ifName[0], 0, rc, ME, "empty name");
    ASSERT_NOT_NULL(pIfaceConf, rc, ME, "NULL");
    ASSERT_TRUE(IS_SPECIFIED(pIfaceConf->type, NL80211_IFTYPE_MAX, NL80211_IFTYPE_UNSPECIFIED),
                rc, ME, "invalid iftype %d", pIfaceConf->type);

    NL_ATTRS(attribs,
             ARR(NL_ATTR_DATA(NL80211_ATTR_IFNAME, strlen(ifName) + 1, ifName),
                 NL_ATTR_VAL(NL80211_ATTR_IFTYPE, pIfaceConf->type),
                 //make the socket owning the interface
                 //to make the interface being destroyed when the socket is closed
                 NL_ATTR(NL80211_ATTR_SOCKET_OWNER)));
    //add mac if provided and valid; driver may support setting mac on interface creation
    bool setMac = (!swl_mac_binIsBroadcast(&pIfaceConf->mac) && !swl_mac_binIsNull(&pIfaceConf->mac));
    if(setMac) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_MAC, SWL_MAC_BIN_LEN, pIfaceConf->mac.bMac));
    }
    if(!ifIndex) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_WIPHY, wiphyId));
    }
    struct getWirelessIfacesData_s requestData = {
        .nrIfacesMax = 1,
        .nrIfaces = 0,
        .pIfaces = calloc(1, sizeof(wld_nl80211_ifaceInfo_t)),
    };
    rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_NEW_INTERFACE, 0,
                                 ifIndex, &attribs, s_getInterfaceInfoCb, &requestData);
    NL_ATTRS_CLEAR(&attribs);
    if(requestData.nrIfaces > 0) {
        wld_nl80211_ifaceInfo_t* pIface = &requestData.pIfaces[0];
        if(setMac && !swl_mac_binMatches(&pIfaceConf->mac, &pIface->mac)) {
            wld_linuxIfUtils_setMacExt(pIface->name, &pIfaceConf->mac);
            wld_linuxIfUtils_getMacExt(pIface->name, &pIface->mac);
        }
        W_SWL_SETPTR(pIfaceInfo, *pIface);
    }
    free(requestData.pIfaces);
    return rc;
}

swl_rc_ne wld_nl80211_newInterfaceExt(wld_nl80211_state_t* state, uint32_t ifIndex, const char* ifName,
                                      wld_nl80211_newIfaceConf_t* pIfaceConf,
                                      wld_nl80211_ifaceInfo_t* pIfaceInfo) {
    return s_newInterfaceExt(state, ifIndex, 0, ifName, pIfaceConf, pIfaceInfo);
}

swl_rc_ne wld_nl80211_newWiphyInterface(wld_nl80211_state_t* state, uint32_t wiphyId, const char* ifName,
                                        wld_nl80211_newIfaceConf_t* pIfaceConf,
                                        wld_nl80211_ifaceInfo_t* pIfaceInfo) {
    return s_newInterfaceExt(state, 0, wiphyId, ifName, pIfaceConf, pIfaceInfo);
}

static wld_nl80211_ifaceInfo_t* s_getWlIfaceByName(const char* ifName, const uint32_t nrWiphyMax, const uint32_t nrWifaceMax,
                                                   wld_nl80211_ifaceInfo_t wlIfaces[nrWiphyMax][nrWifaceMax]) {
    for(uint32_t i = 0; i < nrWiphyMax; i++) {
        for(uint32_t j = 0; j < nrWifaceMax; j++) {
            wld_nl80211_ifaceInfo_t* pIface = &wlIfaces[i][j];
            if((pIface->ifIndex > 0) && (swl_str_matches(pIface->name, ifName))) {
                return pIface;
            }
        }
    }
    return NULL;
}

static uint32_t s_getWlIfacesByWiphy(uint32_t wiphy, const uint32_t maxWiphys, const uint32_t maxWlIfaces,
                                     wld_nl80211_ifaceInfo_t wlIfaces[maxWiphys][maxWlIfaces],
                                     wld_nl80211_ifaceInfo_t** ppWiphyWlIfaces) {
    uint32_t count = 0;
    ASSERT_TRUE((maxWiphys > 0) && (maxWlIfaces > 0), count, ME, "empty list");
    ASSERT_NOT_NULL(wlIfaces, count, ME, "NULL");
    wld_nl80211_ifaceInfo_t* pWiphyWlIfaces = NULL;
    if(wiphy < maxWiphys) {
        pWiphyWlIfaces = wlIfaces[wiphy];
    }
    for(uint32_t i = 0; (i < maxWiphys) && (!count); i++) {
        if((pWiphyWlIfaces == NULL) && (!wlIfaces[i][0].ifIndex)) {
            pWiphyWlIfaces = wlIfaces[i];
        }
        for(uint32_t j = 0; (j < maxWlIfaces) && (wlIfaces[i][j].ifIndex > 0) && (wlIfaces[i][j].wiphy == wiphy); j++) {
            pWiphyWlIfaces = wlIfaces[i];
            count++;
        }
    }
    W_SWL_SETPTR(ppWiphyWlIfaces, pWiphyWlIfaces);
    return count;
}

static swl_rc_ne s_createNewVapIface(wld_nl80211_wiphyInfo_t* pWiphy, const char* ifname, wld_nl80211_ifaceInfo_t* pOutVapInfo) {
    wld_nl80211_newIfaceConf_t ifaceConf;
    memset(&ifaceConf, 0, sizeof(ifaceConf));
    ifaceConf.type = NL80211_IFTYPE_AP;
    wld_nl80211_ifaceInfo_t newVapInfo;
    wld_nl80211_ifaceInfo_t* pNewVapInfo = (pOutVapInfo ? : &newVapInfo);
    memset(pNewVapInfo, 0, sizeof(newVapInfo));
    swl_rc_ne rc = wld_nl80211_newWiphyInterface(wld_nl80211_getSharedState(), pWiphy->wiphy, ifname, &ifaceConf, pNewVapInfo);
    ASSERT_FALSE(rc < SWL_RC_OK, rc, ME, "fail to create vap iface %s on wiphy (%d:%s) (%d:%s)",
                 ifname, pWiphy->wiphy, pWiphy->name, errno, strerror(errno));
    SAH_TRACEZ_WARNING(ME, "created default iface %s netdevIdx %d mac %s on wiphy (%d:%s)",
                       pNewVapInfo->name, pNewVapInfo->ifIndex, swl_typeMacBin_toBuf32(pNewVapInfo->mac).buf,
                       pNewVapInfo->wiphy, pWiphy->name);
    return rc;
}

swl_rc_ne wld_nl80211_addDefaultWiphyInterfacesExt(const char* custIfNamePfx,
                                                   const uint32_t maxWiphys, const uint32_t maxWlIfaces,
                                                   wld_nl80211_ifaceInfo_t wlIfacesInfo[maxWiphys][maxWlIfaces]) {
    const char* ifNamePfx = (!swl_str_isEmpty(custIfNamePfx) ? custIfNamePfx : NL80211_DFLT_IFNAME_PFX);
    wld_nl80211_wiphyInfo_t wiphysInfo[maxWiphys];
    memset(wiphysInfo, 0, sizeof(wiphysInfo));
    uint32_t nrWiphys = 0;
    swl_rc_ne rc = wld_nl80211_getAllWiphyInfo(wld_nl80211_getSharedState(), maxWiphys, wiphysInfo, &nrWiphys);
    ASSERT_EQUALS(rc, SWL_RC_OK, rc, ME, "Fail to get all wiphy");
    ASSERT_TRUE(nrWiphys > 0, SWL_RC_ERROR, ME, "no wiphy detected");
    for(uint8_t i = 0; i < nrWiphys; i++) {
        wld_nl80211_wiphyInfo_t* pWiphy = &wiphysInfo[i];
        wld_nl80211_ifaceInfo_t* pWiphyWlIfaces = NULL;
        uint32_t nrWiphyBands = swl_bit32_getNrSet(pWiphy->freqBandsMask);
        uint32_t nrWiphyWlIfaces = s_getWlIfacesByWiphy(pWiphy->wiphy, maxWiphys, maxWlIfaces, wlIfacesInfo, &pWiphyWlIfaces);
        SAH_TRACEZ_WARNING(ME, "detected wiphy[%d] %s id:%d (maxNr:%d) nrWiphyBands %d (m:0x%x) nrWiphyWlIfaces %d",
                           i, pWiphy->name, pWiphy->wiphy, nrWiphys, nrWiphyBands, pWiphy->freqBandsMask, nrWiphyWlIfaces);
        if((!nrWiphyBands) || (nrWiphyWlIfaces >= nrWiphyBands) || (pWiphyWlIfaces == NULL)) {
            continue;
        }
        uint32_t maxWiphyWlIfaces = (pWiphy->nApMax ? SWL_MIN(pWiphy->nApMax, maxWlIfaces) : maxWlIfaces);
        swl_bit32_t bandMask = pWiphy->freqBandsMask;
        int32_t bandPos;
        while(((bandPos = swl_bit32_getLowest(bandMask)) >= 0) && (nrWiphyWlIfaces < maxWiphyWlIfaces)) {
            SAH_TRACEZ_WARNING(ME, "proc bandPos %d bandMask 0x%x nrWiphyWlIfaces %d maxWiphyWlIfaces %d",
                               bandPos, bandMask, nrWiphyWlIfaces, maxWiphyWlIfaces);
            W_SWL_BIT_CLEAR(bandMask, bandPos);
            char wlIfName[IFNAMSIZ];
            snprintf(wlIfName, sizeof(wlIfName), "%s%d", ifNamePfx, bandPos);
            wld_nl80211_ifaceInfo_t* pExWl = s_getWlIfaceByName(wlIfName, maxWiphys, maxWlIfaces, wlIfacesInfo);
            if(pExWl != NULL) {
                if(pExWl->wiphy == pWiphy->wiphy) {
                    continue;
                }
                swl_str_catFormat(wlIfName, sizeof(wlIfName), "p%d", pWiphy->wiphy);
            }
            s_createNewVapIface(pWiphy, wlIfName, &pWiphyWlIfaces[nrWiphyWlIfaces++]);
        }
    }
    return rc;
}

swl_rc_ne wld_nl80211_delInterface(wld_nl80211_state_t* state, uint32_t ifIndex) {
    return wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_DEL_INTERFACE, 0, ifIndex, NULL);
}

static swl_rc_ne s_registerFrameCb(swl_rc_ne rc, struct nlmsghdr* nlh, void* priv _UNUSED) {
    ASSERT_NOT_NULL(nlh, SWL_RC_ERROR, ME, "NULL");
    ASSERTI_TRUE((rc <= SWL_RC_ERROR), rc, ME, "Register for Mgmt Frame notifications OK");
    ASSERTS_EQUALS(nlh->nlmsg_type, NLMSG_ERROR, SWL_RC_OK, ME, "not error msg");
    struct nlmsgerr* e = (struct nlmsgerr*) nlmsg_data(nlh);
    ASSERT_NOT_NULL(e, rc, ME, "NULL");
    ASSERT_NOT_EQUALS(e->error, -EALREADY, SWL_RC_OK, ME, "Already registered for the mgmt frame notifications");
    SAH_TRACEZ_WARNING(ME, "Fail to register for mgmt frame notifications, error:%d", e->error);
    return SWL_RC_ERROR;
}

swl_rc_ne wld_nl80211_registerFrame(wld_nl80211_state_t* state, uint32_t ifIndex, uint16_t type, const char* pattern, size_t patternLen) {
    NL_ATTRS(attribs,
             ARR(NL_ATTR_VAL(NL80211_ATTR_FRAME_TYPE, type),
                 NL_ATTR_DATA(NL80211_ATTR_FRAME_MATCH, patternLen, pattern)));
    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_REGISTER_ACTION, 0, ifIndex, &attribs, s_registerFrameCb, NULL);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_setInterfaceType(wld_nl80211_state_t* state, uint32_t ifIndex, bool isAp, bool isSta) {
    ASSERT_TRUE((isAp || isSta), WLD_ERROR, ME, "invalid type");
    //no nl80211 interface type for mixed apsta mode
    uint32_t attVal = (isAp ? NL80211_IFTYPE_AP : NL80211_IFTYPE_STATION);
    NL_ATTRS(attribs,
             ARR(NL_ATTR_VAL(NL80211_ATTR_IFTYPE, attVal)));
    swl_rc_ne rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_SET_INTERFACE, 0, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_setInterfaceUse4Mac(wld_nl80211_state_t* state, uint32_t ifIndex, bool use4Mac) {
    uint8_t attVal = use4Mac;
    NL_ATTRS(attribs,
             ARR(NL_ATTR_VAL(NL80211_ATTR_4ADDR, attVal)));
    swl_rc_ne rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_SET_INTERFACE, 0, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

struct getWiphyData_s {
    const uint32_t nrWiphyMax;
    uint32_t nrWiphy;
    wld_nl80211_wiphyInfo_t* pWiphys;
    uint32_t ifIndex;
};
static swl_rc_ne s_getWiphyInfoCb(swl_rc_ne rc, struct nlmsghdr* nlh, void* priv) {
    ASSERTS_FALSE((rc <= SWL_RC_ERROR), rc, ME, "Request error");
    ASSERT_NOT_NULL(nlh, SWL_RC_ERROR, ME, "NULL");
    ASSERTI_EQUALS(nlh->nlmsg_type, g_nl80211DriverIDs.family_id, SWL_RC_OK, ME, "skip msgtype %d", nlh->nlmsg_type);
    struct genlmsghdr* gnlh = (struct genlmsghdr*) nlmsg_data(nlh);
    ASSERTI_EQUALS(gnlh->cmd, NL80211_CMD_NEW_WIPHY, SWL_RC_OK, ME, "unexpected cmd %d", gnlh->cmd);
    struct getWiphyData_s* requestData = (struct getWiphyData_s*) priv;
    ASSERT_NOT_NULL(requestData, SWL_RC_ERROR, ME, "No request data");
    // Parse the netlink message
    struct nlattr* tb[NL80211_ATTR_MAX + 1] = {};
    if(nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL)) {
        SAH_TRACEZ_ERROR(ME, "Failed to parse netlink message");
        return SWL_RC_ERROR;
    }
    ASSERT_NOT_NULL(tb[NL80211_ATTR_GENERATION], SWL_RC_ERROR, ME, "missing genId");
    uint32_t genId = nla_get_u32(tb[NL80211_ATTR_GENERATION]);
    wld_nl80211_wiphyInfo_t* pWiphy = &requestData->pWiphys[0];
    if(requestData->nrWiphy > 0) {
        pWiphy = &requestData->pWiphys[requestData->nrWiphy - 1];
    }
    uint32_t wiphy = wld_nl80211_getWiphy(tb);
    if((pWiphy->genId > 0) &&
       (((pWiphy->genId != genId) && (pWiphy->wiphy == wiphy)) ||
        ((pWiphy->genId == genId) && (pWiphy->wiphy != wiphy) && (requestData->ifIndex != 0)))) {
        SAH_TRACEZ_ERROR(ME, "invalid genId(%d) for received msg of wiphy(%d)", genId, wiphy);
        return SWL_RC_ERROR;
    }
    if((pWiphy->genId != genId) || ((requestData->ifIndex == 0) && (pWiphy->wiphy != wiphy))) {
        if(requestData->nrWiphy >= requestData->nrWiphyMax) {
            SAH_TRACEZ_INFO(ME, "wiphy(%d) skipped: maxWiphys %d reached", wiphy, requestData->nrWiphyMax);
            return SWL_RC_DONE;
        }
        pWiphy = &requestData->pWiphys[requestData->nrWiphy++];
        pWiphy->genId = genId;
    }
    rc = wld_nl80211_parseWiphyInfo(tb, pWiphy);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "parsing failed");
    return SWL_RC_OK;
}
swl_rc_ne wld_nl80211_getWiphyInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_wiphyInfo_t* pWiphyInfo) {
    NL_ATTRS(attribs,
             ARR(NL_ATTR(NL80211_ATTR_SPLIT_WIPHY_DUMP)));
    struct getWiphyData_s requestData = {
        .nrWiphyMax = 1,
        .nrWiphy = 0,
        .pWiphys = calloc(1, sizeof(wld_nl80211_wiphyInfo_t)),
        .ifIndex = ifIndex,
    };
    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_GET_WIPHY, NLM_F_DUMP,
                                           ifIndex, &attribs, s_getWiphyInfoCb, &requestData);
    NL_ATTRS_CLEAR(&attribs);
    if(requestData.nrWiphy == 0) {
        SAH_TRACEZ_ERROR(ME, "no Wiphy found for ifIndex(%d)", ifIndex);
        rc = SWL_RC_ERROR;
    } else if(pWiphyInfo) {
        memcpy(pWiphyInfo, &requestData.pWiphys[0], sizeof(wld_nl80211_wiphyInfo_t));
    }
    free(requestData.pWiphys);
    return rc;
}

static int s_filterWiphyNames(const struct dirent* pEntry) {
    const char* fname = pEntry->d_name;
    if(swl_str_startsWith(fname, "phy")) {
        return 1;
    }
    return 0;
}

int32_t wld_nl80211_countWiphyFromFS(void) {
    struct dirent** namelist;
    const char* sysPath = SYSFS_IEEE80211_PATH;
    int count = 0;
    int n = scandir(sysPath, &namelist, s_filterWiphyNames, alphasort);
    ASSERT_NOT_EQUALS(n, -1, SWL_RC_ERROR, ME, "fail to scan dir %s", sysPath);
    count = n;
    while(n--) {
        free(namelist[n]);
    }
    free(namelist);
    return count;
}

swl_rc_ne wld_nl80211_getAllWiphyInfo(wld_nl80211_state_t* state, const uint32_t nrWiphyMax, wld_nl80211_wiphyInfo_t pWiphyIfs[nrWiphyMax],
                                      uint32_t* pNrWiphy) {
    memset(pWiphyIfs, 0, nrWiphyMax * sizeof(wld_nl80211_wiphyInfo_t));
    NL_ATTRS(attribs,
             ARR(NL_ATTR(NL80211_ATTR_SPLIT_WIPHY_DUMP)));
    uint32_t nrWiphyMaxInt = SWL_MAX((int32_t) nrWiphyMax, SWL_MAX(0, wld_nl80211_countWiphyFromFS()));
    struct getWiphyData_s requestData = {
        .nrWiphyMax = nrWiphyMaxInt,
        .nrWiphy = 0,
        .pWiphys = calloc(nrWiphyMaxInt, sizeof(wld_nl80211_wiphyInfo_t)),
        .ifIndex = 0,
    };
    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_GET_WIPHY, NLM_F_DUMP,
                                           0, &attribs, s_getWiphyInfoCb, &requestData);
    NL_ATTRS_CLEAR(&attribs);
    if(pNrWiphy != NULL) {
        *pNrWiphy = SWL_MIN(requestData.nrWiphy, nrWiphyMax);
    }
    if(requestData.nrWiphy == 0) {
        SAH_TRACEZ_ERROR(ME, "no Wiphy found");
        rc = SWL_RC_ERROR;
    } else if(nrWiphyMax > 0) {
        //reverse copy to restore proper detection order
        for(uint32_t i = 0; (i < nrWiphyMax) && (i < requestData.nrWiphy); i++) {
            pWiphyIfs[i] = requestData.pWiphys[requestData.nrWiphy - i - 1];
        }
    }
    free(requestData.pWiphys);
    return rc;
}

swl_rc_ne wld_nl80211_getVendorWiphyInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_handler_f vendorHandler, void* vendorData) {
    NL_ATTRS(attribs,
             ARR(NL_ATTR(NL80211_ATTR_SPLIT_WIPHY_DUMP)));
    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_GET_WIPHY, NLM_F_DUMP,
                                           ifIndex, &attribs, vendorHandler, vendorData);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

struct getStationData_s {
    const uint32_t nrStationMax;
    uint32_t nrStation;
    wld_nl80211_stationInfo_t* pStationInfo;
};

static swl_rc_ne s_getStationInfoCb(swl_rc_ne rc, struct nlmsghdr* nlh, void* priv) {
    ASSERTS_FALSE((rc <= SWL_RC_ERROR), rc, ME, "Request error");
    ASSERT_NOT_NULL(nlh, SWL_RC_ERROR, ME, "NULL");
    ASSERTI_EQUALS(nlh->nlmsg_type, g_nl80211DriverIDs.family_id, SWL_RC_OK, ME, "skip msgtype %d", nlh->nlmsg_type);

    struct genlmsghdr* gnlh = (struct genlmsghdr*) nlmsg_data(nlh);
    ASSERTI_EQUALS(gnlh->cmd, NL80211_CMD_NEW_STATION, SWL_RC_OK, ME, "unexpected cmd %d", gnlh->cmd);

    struct getStationData_s* requestData = (struct getStationData_s*) priv;
    ASSERT_NOT_NULL(requestData, SWL_RC_ERROR, ME, "No request data");

    // Parse the netlink message
    struct nlattr* tb[NL80211_ATTR_MAX + 1] = {};
    if(nla_parse(tb,
                 NL80211_ATTR_MAX,
                 genlmsg_attrdata(gnlh, 0),
                 genlmsg_attrlen(gnlh, 0),
                 NULL)
       ) {
        SAH_TRACEZ_ERROR(ME, "Failed to parse netlink message");
        return SWL_RC_ERROR;
    }

    wld_nl80211_stationInfo_t stationInfo;
    memset(&stationInfo, 0, sizeof(stationInfo));
    rc = wld_nl80211_parseStationInfo(tb, &stationInfo);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "parsing station info failed");

    if((requestData->pStationInfo == NULL) || (requestData->nrStationMax == 0)) {
        SAH_TRACEZ_INFO(ME, "No device associated available");
        return SWL_RC_OK;
    }
    if(requestData->nrStation >= requestData->nrStationMax) {
        SAH_TRACEZ_INFO(ME, "Device skipped: maxStation %d reached", requestData->nrStationMax);
        return SWL_RC_DONE;
    }

    wld_nl80211_stationInfo_t* pStationInfo = &requestData->pStationInfo[requestData->nrStation];
    memcpy(pStationInfo, &stationInfo, sizeof(*pStationInfo));
    requestData->nrStation++;

    return rc;
}

swl_rc_ne wld_nl80211_getStationInfo(wld_nl80211_state_t* state, uint32_t ifIndex, const swl_macBin_t* pMac, wld_nl80211_stationInfo_t* pStationInfo) {

    ASSERT_NOT_NULL(pMac, SWL_RC_INVALID_PARAM, ME, "NULL");
    NL_ATTRS(attribs, ARR(NL_ATTR_DATA(NL80211_ATTR_MAC, SWL_MAC_BIN_LEN, pMac)));

    struct getStationData_s requestData = {
        .nrStationMax = 1,
        .nrStation = 0,
        .pStationInfo = calloc(1, sizeof(wld_nl80211_stationInfo_t)),
    };

    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_GET_STATION, 0, ifIndex, &attribs, s_getStationInfoCb, &requestData);
    NL_ATTRS_CLEAR(&attribs);

    if(requestData.nrStation == 0) {
        SAH_TRACEZ_NOTICE(ME, "no Station " SWL_MAC_FMT " with ifIndex(%d)", SWL_MAC_ARG(pMac->bMac), ifIndex);
        rc = SWL_RC_INVALID_PARAM;
    } else if(pStationInfo) {
        memcpy(pStationInfo, requestData.pStationInfo, sizeof(wld_nl80211_stationInfo_t));
    }
    free(requestData.pStationInfo);
    return rc;
}

swl_rc_ne wld_nl80211_getAllStationsInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_stationInfo_t** ppStationInfo, uint32_t* pnStation) {

    struct getStationData_s requestData = {
        .nrStationMax = MAXNROF_STAENTRY,
        .nrStation = 0,
        .pStationInfo = calloc(MAXNROF_STAENTRY, sizeof(wld_nl80211_stationInfo_t)),
    };

    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_GET_STATION, NLM_F_DUMP, ifIndex, NULL, s_getStationInfoCb, &requestData);

    if(requestData.nrStation == 0) {
        SAH_TRACEZ_NOTICE(ME, "no Station device with ifIndex(%d)", ifIndex);
        rc = SWL_RC_OK;
    } else if((ppStationInfo != NULL) && (pnStation != NULL)) {
        *ppStationInfo = calloc(requestData.nrStation, sizeof(wld_nl80211_stationInfo_t));
        if(*ppStationInfo == NULL) {
            SAH_TRACEZ_ERROR(ME, "Fail to allocate memory for station info results");
            rc = SWL_RC_ERROR;
        } else {
            *pnStation = requestData.nrStation;
            memcpy(*ppStationInfo, requestData.pStationInfo, requestData.nrStation * sizeof(wld_nl80211_stationInfo_t));
        }
    }
    free(requestData.pStationInfo);
    return rc;
}

struct getSurveyInfoData_s {
    const uint32_t nrChanSurveyInfoMax;
    uint32_t nrChanSurveyInfo;
    wld_nl80211_channelSurveyInfo_t* pChanSurveyInfo;
    swl_freqBandExt_e selectFreqBand;
};

static swl_rc_ne s_getChanSurveyInfoCb(swl_rc_ne rc, struct nlmsghdr* nlh, void* priv) {
    ASSERTS_FALSE((rc <= SWL_RC_ERROR), rc, ME, "Request error");
    ASSERT_NOT_NULL(nlh, SWL_RC_ERROR, ME, "NULL");
    ASSERTI_EQUALS(nlh->nlmsg_type, g_nl80211DriverIDs.family_id, SWL_RC_OK, ME, "skip msgtype %d", nlh->nlmsg_type);
    struct genlmsghdr* gnlh = (struct genlmsghdr*) nlmsg_data(nlh);
    ASSERTI_EQUALS(gnlh->cmd, NL80211_CMD_NEW_SURVEY_RESULTS, SWL_RC_OK, ME, "unexpected cmd %d", gnlh->cmd);

    struct getSurveyInfoData_s* requestData = (struct getSurveyInfoData_s*) priv;
    ASSERT_NOT_NULL(requestData, SWL_RC_ERROR, ME, "No request data");

    struct nlattr* tb[NL80211_ATTR_MAX + 1] = {};

    if(nla_parse(tb,
                 NL80211_ATTR_MAX,
                 genlmsg_attrdata(gnlh, 0),
                 genlmsg_attrlen(gnlh, 0),
                 NULL)
       ) {
        SAH_TRACEZ_ERROR(ME, "Failed to parse netlink message");
        return SWL_RC_ERROR;
    }

    // Parse the netlink message
    wld_nl80211_channelSurveyInfo_t chanSurveyInfo;
    memset(&chanSurveyInfo, 0, sizeof(chanSurveyInfo));
    rc = wld_nl80211_parseChanSurveyInfo(tb, &chanSurveyInfo);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "parsing channel survey info failed");

    if(requestData->selectFreqBand != SWL_FREQ_BAND_EXT_AUTO) {
        swl_chanspec_t entryChSpec = SWL_CHANSPEC_EMPTY;
        if((swl_chanspec_channelFromMHz(&entryChSpec, chanSurveyInfo.frequencyMHz) < SWL_RC_OK) ||
           (entryChSpec.band != requestData->selectFreqBand)) {
            // skip entries out of selected frequency band
            return SWL_RC_OK;
        }
    }

    if((requestData->pChanSurveyInfo == NULL) || (requestData->nrChanSurveyInfoMax == 0)) {
        SAH_TRACEZ_INFO(ME, "No memory available for saving channel survey info");
        return SWL_RC_OK;
    }
    if(requestData->nrChanSurveyInfo >= requestData->nrChanSurveyInfoMax) {
        SAH_TRACEZ_INFO(ME, "Result skipped: maxChanInfo %d reached", requestData->nrChanSurveyInfoMax);
        return SWL_RC_DONE;
    }

    wld_nl80211_channelSurveyInfo_t* pChanSurveyInfo = &requestData->pChanSurveyInfo[requestData->nrChanSurveyInfo];
    memcpy(pChanSurveyInfo, &chanSurveyInfo, sizeof(*pChanSurveyInfo));
    requestData->nrChanSurveyInfo++;

    return rc;
}

swl_rc_ne wld_nl80211_getSurveyInfoExt(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_channelSurveyParam_t* pConfig,
                                       wld_nl80211_channelSurveyInfo_t** ppChanSurveyInfo, uint32_t* pnChanSurveyInfo) {

    struct getSurveyInfoData_s requestData = {
        .nrChanSurveyInfoMax = WLD_MAX_POSSIBLE_CHANNELS,
        .nrChanSurveyInfo = 0,
        .pChanSurveyInfo = calloc(WLD_MAX_POSSIBLE_CHANNELS, sizeof(wld_nl80211_channelSurveyInfo_t)),
        .selectFreqBand = (pConfig ? pConfig->selectFreqBand : SWL_FREQ_BAND_EXT_AUTO),
    };

    swl_rc_ne rc = wld_nl80211_sendCmdSync(state, NL80211_CMD_GET_SURVEY, NLM_F_DUMP, ifIndex, NULL, s_getChanSurveyInfoCb, &requestData);

    if(requestData.nrChanSurveyInfo == 0) {
        SAH_TRACEZ_INFO(ME, "no Channel survey info with ifIndex(%d)", ifIndex);
        rc = SWL_RC_OK;
    } else if((ppChanSurveyInfo != NULL) && (pnChanSurveyInfo != NULL)) {
        *ppChanSurveyInfo = calloc(requestData.nrChanSurveyInfo, sizeof(wld_nl80211_channelSurveyInfo_t));
        if(*ppChanSurveyInfo == NULL) {
            SAH_TRACEZ_ERROR(ME, "Fail to allocate memory for station info results");
            rc = SWL_RC_ERROR;
        } else {
            *pnChanSurveyInfo = requestData.nrChanSurveyInfo;
            memcpy(*ppChanSurveyInfo, requestData.pChanSurveyInfo, requestData.nrChanSurveyInfo * sizeof(wld_nl80211_channelSurveyInfo_t));
        }
    }
    free(requestData.pChanSurveyInfo);
    return rc;
}

swl_rc_ne wld_nl80211_getSurveyInfo(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_channelSurveyInfo_t** ppChanSurveyInfo, uint32_t* pnChanSurveyInfo) {
    return wld_nl80211_getSurveyInfoExt(state, ifIndex, NULL, ppChanSurveyInfo, pnChanSurveyInfo);
}

swl_rc_ne wld_nl80211_setWiphyAntennas(wld_nl80211_state_t* state, uint32_t ifIndex, uint32_t txMapAnt, uint32_t rxMapAnt) {
    NL_ATTRS(attribs,
             ARR(NL_ATTR_VAL(NL80211_ATTR_WIPHY_ANTENNA_TX, txMapAnt),
                 NL_ATTR_VAL(NL80211_ATTR_WIPHY_ANTENNA_RX, rxMapAnt)));
    swl_rc_ne rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_SET_WIPHY, 0, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

static swl_rc_ne s_setTxPower(wld_nl80211_state_t* state, uint32_t ifIndex, enum nl80211_tx_power_setting type, int32_t dbm) {
    NL_ATTRS(attribs, ARR(NL_ATTR_VAL(NL80211_ATTR_WIPHY_TX_POWER_SETTING, type)));
    if(type != NL80211_TX_POWER_AUTOMATIC) {
        dbm *= 100;
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_WIPHY_TX_POWER_LEVEL, dbm));
    }
    swl_rc_ne rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_SET_WIPHY, 0, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_setTxPowerAuto(wld_nl80211_state_t* state, uint32_t ifIndex) {
    return s_setTxPower(state, ifIndex, NL80211_TX_POWER_AUTOMATIC, 0);
}

swl_rc_ne wld_nl80211_setTxPowerFixed(wld_nl80211_state_t* state, uint32_t ifIndex, int32_t dbm) {
    return s_setTxPower(state, ifIndex, NL80211_TX_POWER_FIXED, dbm);
}

swl_rc_ne wld_nl80211_setTxPowerLimited(wld_nl80211_state_t* state, uint32_t ifIndex, int32_t dbm) {
    return s_setTxPower(state, ifIndex, NL80211_TX_POWER_LIMITED, dbm);
}

swl_rc_ne wld_nl80211_getTxPower(wld_nl80211_state_t* state, uint32_t ifIndex, int32_t* dbm) {
    wld_nl80211_ifaceInfo_t ifaceInfo;
    swl_rc_ne rc = wld_nl80211_getInterfaceInfo(state, ifIndex, &ifaceInfo);
    ASSERT_FALSE(rc < SWL_RC_OK, rc, ME, "fail to get radio main iface info");
    //return iface txpower or primary link txpower
    *dbm = ((!ifaceInfo.txPower) && (ifaceInfo.nMloLinks > 0)) ? ifaceInfo.mloLinks[0].txPower : ifaceInfo.txPower;
    return rc;
}

swl_rc_ne wld_nl80211_getMaxTxPowerdBm(wld_nl80211_state_t* state, uint32_t ifIndex, uint32_t freq, int32_t* dbm) {
    int32_t locDbm = 0;
    ASSERT_TRUE(freq > 0, SWL_RC_INVALID_PARAM, ME, "invalid freq");
    wld_nl80211_wiphyInfo_t wiphyInfo;
    memset(&wiphyInfo, 0, sizeof(wiphyInfo));
    swl_rc_ne rc = wld_nl80211_getWiphyInfo(state, ifIndex, &wiphyInfo);
    ASSERT_TRUE(swl_rc_isOk(rc), rc, ME, "fail to get wiphy info");
    for(uint32_t i = 0; i < SWL_FREQ_BAND_MAX; i++) {
        if(wiphyInfo.bands[i].nChans == 0) {
            continue;
        }
        for(uint32_t j = 0; j < wiphyInfo.bands[i].nChans; j++) {
            if(wiphyInfo.bands[i].chans[j].ctrlFreq == freq) {
                locDbm = (int32_t) wiphyInfo.bands[i].chans[j].maxTxPwr;
                break;
            }
        }
    }
    ASSERT_NOT_EQUALS(locDbm, 0, SWL_RC_ERROR, ME, "maxPow unknown for freq %d", freq);
    W_SWL_SETPTR(dbm, locDbm);
    return SWL_RC_OK;
}

static uint32_t s_getScanFlags(wld_nl80211_scanFlags_t* pFlags) {
    uint32_t flags = 0;
    ASSERTS_NOT_NULL(pFlags, flags, ME, "NULL");
    if(pFlags->flush) {
        flags |= NL80211_SCAN_FLAG_FLUSH;
    }
    if(pFlags->force) {
        flags |= NL80211_SCAN_FLAG_AP;
    }
    return flags;
}

swl_rc_ne wld_nl80211_startScan(wld_nl80211_state_t* state, uint32_t ifIndex, wld_nl80211_scanParams_t* params) {
    SAH_TRACEZ_IN(ME);
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    NL_ATTRS(attribs, ARR());
    if(params) {
        uint32_t flags = s_getScanFlags(&params->flags);
        if(flags) {
            NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_SCAN_FLAGS, flags));
            SAH_TRACEZ_INFO(ME, "iface:%d, added scan flags attribute(%d)", ifIndex, swl_unLiList_size(&attribs));
        }
        swl_unLiListIt_t it;
        if(swl_unLiList_size(&params->ssids) > 0) {
            NL_ATTR_NESTED(ssidsAttr, NL80211_ATTR_SCAN_SSIDS);
            SAH_TRACEZ_INFO(ME, "Trying to add scan ssids(%d)", swl_unLiList_size(&params->ssids));
            swl_unLiList_for_each(it, &params->ssids) {
                char* ssid = *(swl_unLiList_data(&it, char**));
                if(ssid == NULL) {
                    continue;
                }
                SAH_TRACEZ_INFO(ME, "Scan for ssid %s ", ssid);
                NL_ATTRS_ADD(&ssidsAttr.data.attribs,
                             NL_ATTR_DATA(swl_unLiList_size(&ssidsAttr.data.attribs) + 1, strlen(ssid), ssid));
            }
            if(swl_unLiList_size(&ssidsAttr.data.attribs) > 0) {
                swl_unLiList_add(&attribs, &ssidsAttr);
            } else {
                wld_nl80211_cleanNlAttr(&ssidsAttr);
            }
        }

        if(swl_mac_binIsNull(&params->bssid) == false) {
            NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_MAC, SWL_MAC_BIN_LEN, params->bssid.bMac));//some legacy implementations use NL80211_ATTR_MAC instead of NL80211_ATTR_BSSID
            NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_BSSID, SWL_MAC_BIN_LEN, params->bssid.bMac));
            SAH_TRACEZ_INFO(ME, "Scan for bssid  "SWL_MAC_FMT "", SWL_MAC_ARG(params->bssid.bMac));

        }

        if(swl_unLiList_size(&params->freqs) > 0) {
            NL_ATTR_NESTED(freqsAttr, NL80211_ATTR_SCAN_FREQUENCIES);
            swl_unLiList_for_each(it, &params->freqs) {
                uint32_t* pFreq = swl_unLiList_data(&it, uint32_t*);
                if((pFreq == NULL) || (*pFreq == 0)) {
                    continue;
                }
                SAH_TRACEZ_INFO(ME, "Scan over frequency %u MHz", *pFreq);
                NL_ATTRS_ADD(&freqsAttr.data.attribs,
                             NL_ATTR_DATA(swl_unLiList_size(&freqsAttr.data.attribs) + 1, sizeof(*pFreq), pFreq));
            }
            if(swl_unLiList_size(&freqsAttr.data.attribs) > 0) {
                swl_unLiList_add(&attribs, &freqsAttr);
            } else {
                wld_nl80211_cleanNlAttr(&freqsAttr);
            }
        }

        if((params->iesLen > 0) && (params->ies != NULL)) {
            NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_IE, params->iesLen, params->ies));
            SAH_TRACEZ_INFO(ME, "Scan probe with extra IEs");
        }

        if(params->measDuration > 0) {
            uint16_t scanDurationTu = wld_nl80211_ms2tu(params->measDuration);
            NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_MEASUREMENT_DURATION, scanDurationTu));
            // only mandatory when explicitly defined in user conf
            if(params->measDurationMandatory) {
                NL_ATTRS_ADD(&attribs, NL_ATTR(NL80211_ATTR_MEASUREMENT_DURATION_MANDATORY));
            }
        }
    }
    rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_TRIGGER_SCAN, 0, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_abortScan(wld_nl80211_state_t* state, uint32_t ifIndex) {
    return wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_ABORT_SCAN, 0, ifIndex, NULL);
}

struct getScanResultsData_s {
    swl_freqBandExt_e band;
    scanResultsCb_f fScanResultsCb;
    wld_scanResults_t results;
    void* priv;
};
static swl_rc_ne s_scanResultCb(swl_rc_ne rc, struct nlmsghdr* nlh, void* priv) {
    wld_scanResultSSID_t* pResult = NULL;
    struct getScanResultsData_s* requestData = (struct getScanResultsData_s*) priv;
    if(rc <= SWL_RC_ERROR) {
        goto scanFinish;
    }
    if(nlh->nlmsg_type == NLMSG_DONE) {
        rc = SWL_RC_DONE;
        goto scanFinish;
    }
    if((nlh->nlmsg_type != g_nl80211DriverIDs.family_id) &&
       (nlh->nlmsg_type != g_nl80211DriverIDs.scan_mcgrp_id)) {
        SAH_TRACEZ_INFO(ME, "skip msgtype %d", nlh->nlmsg_type);
        return SWL_RC_OK;
    }
    struct genlmsghdr* gnlh = (struct genlmsghdr*) nlmsg_data(nlh);
    ASSERTI_EQUALS(gnlh->cmd, NL80211_CMD_NEW_SCAN_RESULTS, SWL_RC_OK, ME, "unexpected cmd %d", gnlh->cmd);
    wld_scanResultSSID_t result;
    memset(&result, 0, sizeof(result));
    struct nlattr* tb[NL80211_ATTR_MAX + 1] = {};
    swl_freqBandExt_e band = requestData ? requestData->band : SWL_FREQ_BAND_EXT_AUTO;
    if((nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL) != 0) ||
       ((rc = wld_nl80211_parseScanResultPerFreqBand(tb, &result, band)) < SWL_RC_OK)) {
        SAH_TRACEZ_ERROR(ME, "Failed to parse nl msg evt(%d)", gnlh->cmd);
        goto scanFinish;
    }
    if(rc == SWL_RC_CONTINUE) {
        SAH_TRACEZ_INFO(ME, "skip nl msg due to partial parsing");
    } else if(requestData) {
        pResult = calloc(1, sizeof(wld_scanResultSSID_t));
        if(pResult == NULL) {
            SAH_TRACEZ_ERROR(ME, "fail to alloc scan result element");
            goto scanFinish;
        }
        memcpy(pResult, &result, sizeof(wld_scanResultSSID_t));
        amxc_llist_it_init(&pResult->it);
        amxc_llist_append(&requestData->results.ssids, &pResult->it);
    }
    ASSERTS_FALSE((nlh->nlmsg_flags & NLM_F_MULTI), SWL_RC_OK, ME, "expecting other nl msg");
scanFinish:
    ASSERTS_NOT_NULL(requestData, rc, ME, "no request data");
    SAH_TRACEZ_INFO(ME, "rc:%d nResults:%d", rc, (int) amxc_llist_size(&requestData->results.ssids));
    amxc_llist_for_each(it, &requestData->results.ssids) {
        pResult = amxc_llist_it_get_data(it, wld_scanResultSSID_t, it);
        SAH_TRACEZ_INFO(ME, "scan result entry: bssid("SWL_MAC_FMT ") ssid(%s) signal(%d dbm)",
                        SWL_MAC_ARG(pResult->bssid.bMac), pResult->ssid, pResult->rssi);
    }
    if(requestData->fScanResultsCb) {
        requestData->fScanResultsCb(requestData->priv, rc, &requestData->results);
    }
    SAH_TRACEZ_INFO(ME, "clean request data");
    amxc_llist_for_each(it, &requestData->results.ssids) {
        pResult = amxc_llist_it_get_data(it, wld_scanResultSSID_t, it);
        amxc_llist_it_take(&pResult->it);
        free(pResult);
    }
    free(requestData);
    return rc;
}

swl_rc_ne wld_nl80211_getScanResultsPerFreqBand(wld_nl80211_state_t* state, uint32_t ifIndex, void* priv, scanResultsCb_f fScanResultsCb, swl_freqBandExt_e band) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_NOT_NULL(state, rc, ME, "NULL");
    struct getScanResultsData_s* pScanResultsData = calloc(1, sizeof(struct getScanResultsData_s));
    if(pScanResultsData == NULL) {
        SAH_TRACEZ_ERROR(ME, "Fail to alloc getScanResults req data");
        if(fScanResultsCb) {
            fScanResultsCb(priv, rc, NULL);
        }
        return rc;
    }
    pScanResultsData->band = band;
    pScanResultsData->fScanResultsCb = fScanResultsCb;
    amxc_llist_init(&pScanResultsData->results.ssids);
    pScanResultsData->priv = priv;
    rc = wld_nl80211_sendCmd(false, state, NL80211_CMD_GET_SCAN, NLM_F_DUMP, ifIndex, NULL, s_scanResultCb, pScanResultsData, NULL);
    return rc;
}

swl_rc_ne wld_nl80211_getScanResults(wld_nl80211_state_t* state, uint32_t ifIndex, void* priv, scanResultsCb_f fScanResultsCb) {
    return wld_nl80211_getScanResultsPerFreqBand(state, ifIndex, priv, fScanResultsCb, SWL_FREQ_BAND_EXT_AUTO);
}

swl_rc_ne wld_nl80211_setRegDomain(wld_nl80211_state_t* state, uint32_t wiphy, const char* alpha2) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_EQUALS(swl_str_len(alpha2), 2, rc, ME, "invalid alpha2");
    NL_ATTRS(attribs, ARR());
    if((wiphy != WLD_NL80211_ID_ANY) && (wiphy != WLD_NL80211_ID_UNDEF)) {
        //No specific wiphy for global regulatory domain
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_WIPHY, wiphy));
    }
    NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_REG_ALPHA2, swl_str_len(alpha2) + 1, alpha2));
    rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_REQ_SET_REG, 0, 0, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_bgDfsStart(wld_nl80211_state_t* state, uint32_t ifIndex, int8_t ifMloLinkId, swl_chanspec_t bgDfsChanspec) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_NOT_NULL(state, rc, ME, "NULL");

    SAH_TRACEZ_INFO(ME, "Starting BG_DFS ifIndex(%d) %u/%s",
                    ifIndex, bgDfsChanspec.channel, swl_bandwidth_str[bgDfsChanspec.bandwidth]);

    NL_ATTRS(attribs, ARR());
    if((ifMloLinkId != MLO_LINK_ID_UNKNOWN) && (ifMloLinkId >= 0)) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_MLO_LINK_ID, ifMloLinkId));
    }
    NL_ATTRS_ADD(&attribs, NL_ATTR(NL80211_ATTR_RADAR_BACKGROUND));

    uint32_t channelWidth = wld_nl80211_bwSwlToNl(bgDfsChanspec.bandwidth);
    NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_CHANNEL_WIDTH, channelWidth));

    uint32_t channelFreqMHz;
    swl_chanspec_channelToMHz(&bgDfsChanspec, &channelFreqMHz);
    NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_WIPHY_FREQ, channelFreqMHz));

    swl_channel_t centerChannel = swl_chanspec_getCentreChannel(&bgDfsChanspec);
    bgDfsChanspec.channel = centerChannel;
    uint32_t centerFreqMHz;
    swl_chanspec_channelToMHz(&bgDfsChanspec, &centerFreqMHz);
    NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_CENTER_FREQ1, centerFreqMHz));

    rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_RADAR_DETECT, 0, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_bgDfsStop(wld_nl80211_state_t* state, uint32_t ifIndex, int8_t ifMloLinkId) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_NOT_NULL(state, rc, ME, "NULL");

    SAH_TRACEZ_INFO(ME, "Stopping BG_DFS ifIndex(%d)", ifIndex);

    NL_ATTRS(attribs, ARR());
    if((ifMloLinkId != MLO_LINK_ID_UNKNOWN) && (ifMloLinkId >= 0)) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_MLO_LINK_ID, ifMloLinkId));
    }

    rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_STOP_BGRADAR_DETECT, 0, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_sendVendorSubCmd(wld_nl80211_state_t* state, uint32_t oui, int subcmd, void* data, int dataLen,
                                       bool isSync, bool withAck, uint32_t flags, uint32_t ifIndex, uint64_t wDevId,
                                       wld_nl80211_handler_f handler, void* priv) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_NOT_NULL(state, rc, ME, "NULL");
    ASSERT_FALSE((wDevId == 0) && (ifIndex == 0), rc, ME, "devices wDevId and index are 0");

    NL_ATTRS(attribs,
             ARR(
                 NL_ATTR_VAL(NL80211_ATTR_VENDOR_ID, oui),
                 NL_ATTR_VAL(NL80211_ATTR_VENDOR_SUBCMD, subcmd)
                 )
             );
    if(wDevId) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_WDEV, wDevId));
    }
    if(dataLen) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_VENDOR_DATA, dataLen, data));
    }

    if(withAck) {
        rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_VENDOR, flags, ifIndex, &attribs);
    } else {
        rc = wld_nl80211_sendCmd(isSync, state, NL80211_CMD_VENDOR, flags, ifIndex, &attribs, handler, priv, NULL);
    }
    NL_ATTRS_CLEAR(&attribs);

    return rc;
}

swl_rc_ne wld_nl80211_sendVendorSubCmdAttr(wld_nl80211_state_t* state, uint32_t oui, int subcmd, wld_nl80211_nlAttr_t* vendorAttr,
                                           bool isSync, bool withAck, uint32_t flags, uint32_t ifIndex, uint64_t wDevId,
                                           wld_nl80211_handler_f handler, void* priv) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_NOT_NULL(state, rc, ME, "NULL");
    ASSERT_FALSE((wDevId == 0) && (ifIndex == 0), rc, ME, "devices wDevId and index are 0");

    NL_ATTRS(attribs,
             ARR(
                 NL_ATTR_VAL(NL80211_ATTR_VENDOR_ID, oui),
                 NL_ATTR_VAL(NL80211_ATTR_VENDOR_SUBCMD, subcmd)
                 )
             );
    if(wDevId) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_WDEV, wDevId));
    }

    swl_unLiList_add(&attribs, vendorAttr);

    if(withAck) {
        rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_VENDOR, flags, ifIndex, &attribs);
    } else {
        rc = wld_nl80211_sendCmd(isSync, state, NL80211_CMD_VENDOR, flags, ifIndex, &attribs, handler, priv, NULL);
    }
    NL_ATTRS_CLEAR(&attribs);

    return rc;
}

swl_rc_ne wld_nl80211_sendManagementFrameCmd(wld_nl80211_state_t* state, swl_80211_mgmtFrameControl_t* fc, swl_bit8_t* data, size_t dataLen,
                                             swl_chanspec_t* chanspec, swl_macBin_t* src, swl_macBin_t* dst, swl_macBin_t* bssid, uint32_t flags,
                                             uint32_t ifIndex, int8_t ifMloLinkId) {
    swl_rc_ne rc = SWL_RC_INVALID_PARAM;
    ASSERT_NOT_NULL(state, rc, ME, "NULL");
    ASSERT_NOT_NULL(chanspec, rc, ME, "NULL");
    ASSERT_NOT_NULL(dst, rc, ME, "NULL");
    ASSERT_NOT_NULL(src, rc, ME, "NULL");
    ASSERT_NOT_NULL(bssid, rc, ME, "NULL");
    uint32_t frequency = 0;
    rc = swl_chanspec_channelToMHz(chanspec, &frequency);
    ASSERT_EQUALS(rc, SWL_RC_OK, rc, ME, "NULL");

    size_t frameLen = sizeof(swl_80211_mgmtFrame_t) - 1 + dataLen;
    swl_bit8_t frame[frameLen];
    memset(&frame, 0, frameLen);
    swl_80211_mgmtFrame_t* hdr = (swl_80211_mgmtFrame_t*) &frame;
    memcpy(&hdr->fc, fc, sizeof(swl_80211_mgmtFrameControl_t));
    memcpy(&hdr->destination, dst->bMac, SWL_MAC_BIN_LEN);
    memcpy(&hdr->transmitter, src->bMac, SWL_MAC_BIN_LEN);
    memcpy(&hdr->bssid, bssid->bMac, SWL_MAC_BIN_LEN);
    memcpy(&hdr->data, data, dataLen);

    NL_ATTRS(attribs,
             ARR(NL_ATTR_VAL(NL80211_ATTR_WIPHY_FREQ, frequency),
                 NL_ATTR(NL80211_ATTR_OFFCHANNEL_TX_OK),
                 NL_ATTR(NL80211_ATTR_TX_NO_CCK_RATE),
                 NL_ATTR(NL80211_ATTR_DONT_WAIT_FOR_ACK),
                 NL_ATTR_DATA(NL80211_ATTR_FRAME, frameLen, frame)));
    if((ifMloLinkId != MLO_LINK_ID_UNKNOWN) && (ifMloLinkId >= 0)) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_VAL(NL80211_ATTR_MLO_LINK_ID, ifMloLinkId));
    }
    rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_ACTION, flags, ifIndex, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    return rc;
}

swl_rc_ne wld_nl80211_getVendorDataFromVendorMsg(swl_rc_ne rc, struct nlmsghdr* nlh, void** data, size_t* dataLen) {
    ASSERT_FALSE((rc <= SWL_RC_ERROR), rc, ME, "Request error");
    ASSERT_NOT_NULL(nlh, SWL_RC_ERROR, ME, "NULL");

    struct genlmsghdr* gnlh = (struct genlmsghdr*) nlmsg_data(nlh);
    ASSERT_EQUALS(gnlh->cmd, NL80211_CMD_VENDOR, SWL_RC_OK, ME, "unexpected cmd %d", gnlh->cmd);

    struct nlattr* tb[NL80211_ATTR_MAX + 1] = {};
    if(nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL)) {
        SAH_TRACEZ_ERROR(ME, "Failed to parse netlink message");
        return SWL_RC_ERROR;
    }

    nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL);

    // parse data table
    ASSERT_NOT_NULL(tb[NL80211_ATTR_VENDOR_DATA], SWL_RC_ERROR, ME, "NULL");
    W_SWL_SETPTR(data, nla_data(tb[NL80211_ATTR_VENDOR_DATA]));
    W_SWL_SETPTR(dataLen, nla_len(tb[NL80211_ATTR_VENDOR_DATA]));

    return rc;
}

swl_rc_ne wld_nl80211_chanSpecNlToSwl(swl_chanspec_t* pSwlChanSpec, wld_nl80211_chanSpec_t* pNlChanSpec) {
    ASSERT_NOT_NULL(pSwlChanSpec, SWL_RC_INVALID_PARAM, ME, "NULL");
    ASSERT_NOT_NULL(pNlChanSpec, SWL_RC_INVALID_PARAM, ME, "NULL");
    swl_chanspec_t ctrlChanspec;
    swl_chanspec_t centerChanspec;
    swl_rc_ne rc = swl_chanspec_channelFromMHz(&ctrlChanspec, pNlChanSpec->ctrlFreq);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "fail to get ctrl channel");
    rc = swl_chanspec_channelFromMHz(&centerChanspec, pNlChanSpec->centerFreq1);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "fail to get center channel");
    rc = swl_chanspec_fromFreqCtrlCentre(pSwlChanSpec, ctrlChanspec.band, ctrlChanspec.channel, centerChanspec.channel);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "fail to get Channel Spec");
    return SWL_RC_OK;
}

swl_rc_ne wld_nl80211_getChanSpecFromIfaceInfo(swl_chanspec_t* pChanSpec, wld_nl80211_ifaceInfo_t* pIfaceInfo) {
    ASSERT_NOT_NULL(pIfaceInfo, SWL_RC_INVALID_PARAM, ME, "NULL");
    return wld_nl80211_chanSpecNlToSwl(pChanSpec, &pIfaceInfo->chanSpec);
}

swl_rc_ne wld_nl80211_getChanSpec(wld_nl80211_state_t* state, uint32_t ifIndex, swl_chanspec_t* pChanSpec) {
    ASSERTS_NOT_NULL(pChanSpec, SWL_RC_INVALID_PARAM, ME, "NULL");
    ASSERTS_TRUE(ifIndex > 0, SWL_RC_INVALID_PARAM, ME, "invalid ifindex");
    wld_nl80211_ifaceInfo_t ifaceInfo;
    swl_rc_ne rc = wld_nl80211_getInterfaceInfo(state, ifIndex, &ifaceInfo);
    ASSERTS_FALSE(rc < SWL_RC_OK, rc, ME, "no iface info");
    //return iface chanspec or primary link chanspec
    wld_nl80211_chanSpec_t* pNlChspec = &ifaceInfo.chanSpec;
    if((!pNlChspec->ctrlFreq) && (ifaceInfo.nMloLinks > 0)) {
        pNlChspec = &ifaceInfo.mloLinks[0].chanSpec;
    }
    rc = wld_nl80211_chanSpecNlToSwl(pChanSpec, pNlChspec);
    return rc;
}

const wld_nl80211_ifaceMloLinkInfo_t* wld_nl80211_fetchIfaceMloLinkByMac(wld_nl80211_ifaceInfo_t* pIface, swl_macBin_t* pLinkMac) {
    ASSERTS_NOT_NULL(pIface, NULL, ME, "NULL");
    ASSERTS_NOT_NULL(pLinkMac, NULL, ME, "NULL");
    ASSERTS_FALSE(swl_mac_binIsNull(pLinkMac), NULL, ME, "invalid linkMac");
    for(uint32_t i = 0; i < pIface->nMloLinks; i++) {
        const wld_nl80211_ifaceMloLinkInfo_t* pLink = &pIface->mloLinks[i];
        if(swl_mac_binMatches(&pLink->link.linkMac, pLinkMac)) {
            return pLink;
        }
    }
    return NULL;
}

const wld_nl80211_ifaceMloLinkInfo_t* wld_nl80211_fetchIfaceMloLinkById(wld_nl80211_ifaceInfo_t* pIface, int32_t linkId) {
    ASSERTS_NOT_NULL(pIface, NULL, ME, "NULL");
    ASSERTS_TRUE(linkId >= 0, NULL, ME, "invalid linkId");
    for(uint32_t i = 0; i < pIface->nMloLinks; i++) {
        const wld_nl80211_ifaceMloLinkInfo_t* pLink = &pIface->mloLinks[i];
        if(pLink->link.linkId == linkId) {
            return pLink;
        }
    }
    return NULL;
}

const wld_nl80211_ifaceMloLinkInfo_t* wld_nl80211_getIfaceMloLinkAtPos(wld_nl80211_ifaceInfo_t* pIface, uint32_t linkPos) {
    ASSERTS_NOT_NULL(pIface, NULL, ME, "NULL");
    ASSERTS_TRUE((pIface->nMloLinks > 0) && (linkPos < pIface->nMloLinks), NULL, ME, "no mlo Link");
    return &pIface->mloLinks[linkPos];
}

swl_rc_ne wld_nl80211_findMldIfaceByLinkMac(wld_nl80211_state_t* state, swl_macBin_t* pLinkMac, wld_nl80211_ifaceInfo_t* pIface, int32_t* pLinkId) {
    if(pIface != NULL) {
        memset(pIface, 0, sizeof(*pIface));
    }
    W_SWL_SETPTR(pLinkId, -1);
    ASSERT_NOT_NULL(pLinkMac, SWL_RC_INVALID_PARAM, ME, "NULL mac");
    ASSERTI_FALSE(swl_mac_binIsNull(pLinkMac), SWL_RC_INVALID_PARAM, ME, "empty mac");
    struct getWirelessIfacesData_s requestData = {0, 0, NULL};
    swl_rc_ne rc = wld_nl80211_getInterfacesList(state, &requestData.pIfaces, requestData.nrIfacesMax, &requestData.nrIfaces);
    if(rc >= SWL_RC_OK) {
        uint32_t i;
        for(i = 0; i < requestData.nrIfaces; i++) {
            wld_nl80211_ifaceInfo_t* pTmpIface = &requestData.pIfaces[i];
            const wld_nl80211_ifaceMloLinkInfo_t* pTmpLinkInfo = wld_nl80211_fetchIfaceMloLinkByMac(pTmpIface, pLinkMac);
            if(pTmpLinkInfo == NULL) {
                continue;
            }
            if(pIface != NULL) {
                memcpy(pIface, pTmpIface, sizeof(*pIface));
            }
            W_SWL_SETPTR(pLinkId, pTmpLinkInfo->link.linkId);
            break;
        }
        if(i == requestData.nrIfaces) {
            rc = SWL_RC_NOT_AVAILABLE;
        }
    }
    W_SWL_FREE(requestData.pIfaces);
    return rc;
}

static void s_parseRawDevIE(void* pUserData, swl_parsingArgs_t* pParsingArgs _UNUSED, swl_80211_elId_ne elId, uint8_t len, uint8_t* frm) {
    raw_dev_ies_t* pRawDevIE = (raw_dev_ies_t*) pUserData;

    switch(elId) {
    case SWL_80211_EL_ID_HT_CAP:
        memcpy(&pRawDevIE->htCap, (swl_80211_htCapIE_t*) frm, len);
        break;
    case SWL_80211_EL_ID_VHT_CAP:
        memcpy(&pRawDevIE->vhtCap, (swl_80211_vhtCapIE_t*) frm, len);
        break;
    case SWL_80211_EL_ID_SUP_RATES:
        pRawDevIE->supported_rates_len = len;
        memcpy(&pRawDevIE->supported_rates, frm, len); // Copy over raw supported rates data
    default:
        break;
    }
}


SWL_TABLE(rawAssocRequestIEParseTable,
          ARR(uint8_t index; void* val; ),
          ARR(swl_type_uint8, swl_type_voidPtr),
          ARR(
              {SWL_80211_EL_ID_HT_CAP, (void*) s_parseRawDevIE},
              {SWL_80211_EL_ID_VHT_CAP, (void*) s_parseRawDevIE},
              {SWL_80211_EL_ID_SUP_RATES, (void*) s_parseRawDevIE},
              )
          );

swl_rc_ne wld_nl80211_addStation(wld_nl80211_state_t* state, uint32_t iface_idx, swl_macBin_t* mac, uint8_t* assoc_req_data,
                                 size_t assoc_req_len, uint16_t aid, uint16_t flags) {

    SAH_TRACEZ_IN(ME);

    swl_80211_assocReqFrameBody_t* assoc_req_frame = (swl_80211_assocReqFrameBody_t*) assoc_req_data;
    uint16_t capability = assoc_req_frame->capInfo;
    uint16_t listen_interval = assoc_req_frame->listenInterval;
    // Parse IEs for the rest
    raw_dev_ies_t rawIEs;
    memset(&rawIEs, 0, sizeof(rawIEs));
    ssize_t parsedLen = swl_80211_parseCustomInfoElementsBuffer(&rawIEs, &rawAssocRequestIEParseTable, NULL,
                                                                assoc_req_len - offsetof(swl_80211_assocReqFrameBody_t, data),
                                                                assoc_req_frame->data);

    char rates_str[rawIEs.supported_rates_len * 2 + 1];
    memset(rates_str, 0, sizeof(rates_str));
    swl_hex_fromBytes(rates_str, sizeof(rates_str), rawIEs.supported_rates, rawIEs.supported_rates_len, false);

    char ht_cap_str[sizeof(rawIEs.htCap) * 2 + 1];
    memset(ht_cap_str, 0, sizeof(ht_cap_str));
    swl_hex_fromBytes(ht_cap_str, sizeof(ht_cap_str), (uint8_t*) &rawIEs.htCap, sizeof(rawIEs.htCap), false);

    char vht_cap_str[sizeof(rawIEs.vhtCap) * 2 + 1];
    memset(vht_cap_str, 0, sizeof(vht_cap_str));
    swl_hex_fromBytes(vht_cap_str, sizeof(vht_cap_str), (uint8_t*) &rawIEs.vhtCap, sizeof(rawIEs.vhtCap), false);

    struct nl80211_sta_flag_update sta_flags = {0};
    sta_flags.set = flags;
    sta_flags.mask = flags;

    SAH_TRACEZ_INFO(ME, "Adding station with MAC '" SWL_MAC_FMT "' to interface '%d' with AID '%d'", SWL_MAC_ARG(mac->bMac), iface_idx, aid);
    SAH_TRACEZ_INFO(ME, "Capability: %x, Listen Interval: %d, Flags: %u", capability, listen_interval, sta_flags.set);
    SAH_TRACEZ_INFO(ME, "Supported Rates: %s, HT Capabilities: %s, VHT Capabilities: %s", rates_str, ht_cap_str, vht_cap_str);

    // Setup NL80211 attributes
    NL_ATTRS(attribs,
             ARR(NL_ATTR_VAL(NL80211_ATTR_IFINDEX, iface_idx),
                 NL_ATTR_DATA(NL80211_ATTR_MAC, ETHER_ADDR_LEN, mac->bMac),
                 NL_ATTR_DATA(NL80211_ATTR_STA_SUPPORTED_RATES, rawIEs.supported_rates_len, rawIEs.supported_rates),
                 NL_ATTR_DATA(NL80211_ATTR_STA_FLAGS2, sizeof(sta_flags), &sta_flags),
                 NL_ATTR_VAL(NL80211_ATTR_STA_AID, aid),
                 NL_ATTR_VAL(NL80211_ATTR_STA_LISTEN_INTERVAL, listen_interval),
                 NL_ATTR_VAL(NL80211_ATTR_STA_CAPABILITY, capability)
                 ));

    if(rawIEs.htCap.htCapInfo != 0) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_HT_CAPABILITY, sizeof(rawIEs.htCap), &rawIEs.htCap));
    }
    if((rawIEs.vhtCap.vhtCapInfo != 0) && NL80211_ATTR_IS_SPECIFIED(NL80211_ATTR_VHT_CAPABILITY)) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_VHT_CAPABILITY, sizeof(rawIEs.vhtCap), &rawIEs.vhtCap));
    }

    if((rawIEs.heCap.heCaps != 0) && NL80211_ATTR_IS_SPECIFIED(NL80211_ATTR_HE_CAPABILITY)) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_HE_CAPABILITY, sizeof(rawIEs.heCap), &rawIEs.heCap));
    }

    // Check that `rawIEs.ehtCap` is not empty before adding it to the attributes
    uint8_t zero_buf[sizeof(rawIEs.ehtCap)] = {0};
    if((memcmp((uint8_t*) &rawIEs.ehtCap, zero_buf, sizeof(rawIEs.ehtCap)) != 0) && NL80211_ATTR_IS_SPECIFIED(NL80211_ATTR_EHT_CAPABILITY)) {
        NL_ATTRS_ADD(&attribs, NL_ATTR_DATA(NL80211_ATTR_EHT_CAPABILITY, sizeof(rawIEs.ehtCap), &rawIEs.ehtCap));
    }

    // Send NL80211_CMD_NEW_STATION command
    swl_rc_ne rc = wld_nl80211_sendCmdSyncWithAck(state, NL80211_CMD_NEW_STATION, 0, iface_idx, &attribs);
    NL_ATTRS_CLEAR(&attribs);
    SAH_TRACEZ_OUT(ME);
    return rc;
}


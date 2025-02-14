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
#include <errno.h>
#include <swl/fileOps/swl_fileUtils.h>
#include <swl/fileOps/swl_mapWriterKVP.h>
#include "wld.h"
#include "wld_util.h"
#include "wld_hostapd_cfgManager.h"
#include "wld_hostapd_cfgManager_priv.h"
#include "wld_hostapd_cfgFile.h"
#include "wld_radio.h"
#include "wld_rad_hostapd_api.h"

#define ME "fileMgr"

/**
 * @brief create a vap map for interface/bss section
 *
 * @param isInterface if the hostapdVapInfo is an interface or bss
 * @param bssName the interface/bss name
 *
 * @return a Non NULL pointer to a wld_hostapdVapInfo_t on success. Otherwise, NULL
 */
static wld_hostapdVapInfo_t* s_createHostapdVapInfo(bool isInterface, char* bssName) {
    wld_hostapdVapInfo_t* vapInfo = calloc(1, sizeof(wld_hostapdVapInfo_t));
    ASSERTS_NOT_NULL(vapInfo, NULL, ME, "NULL");
    vapInfo->isInterface = isInterface;
    vapInfo->bssName = strdup(bssName);
    swl_mapChar_init(&(vapInfo->vapParams));
    return vapInfo;
}

/**
 * @brief delete a vap map for interface/bss section
 *
 * @param isInterface if the hostapdVapInfo is an interface or bss
 *
 * @return void
 */
static void s_deleteHostapdVapInfo(wld_hostapdVapInfo_t* vapInfo) {
    ASSERTS_NOT_NULL(vapInfo, , ME, "NULL");
    free(vapInfo->bssName);
    vapInfo->bssName = NULL;
    swl_mapChar_cleanup(&(vapInfo->vapParams));
    amxc_llist_it_take(&vapInfo->it);
    free(vapInfo);
}

/**
 * @brief allocate and initialize wld_hostapd_config_t structure from the vap list
 *
 * @param conf the structure mapping the content of hostapd configuration file
 * @param llAP vap linked list
 *
 * @return true on success.
 * Otherwise, false:
 */
static bool s_initConfig(wld_hostapd_config_t** pConf, amxc_llist_t* pllAP) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(pConf, false, ME, "NULL");
    wld_hostapd_config_t* config = calloc(1, sizeof(wld_hostapd_config_t));
    ASSERT_NOT_NULL(config, false, ME, "Config allocation failed");
    *pConf = config;
    // Init the header map
    swl_mapChar_init(&(config->header));
    amxc_llist_init(&config->vaps);
    amxc_llist_for_each(ap_it, pllAP) {
        T_AccessPoint* pAp = amxc_llist_it_get_data(ap_it, T_AccessPoint, it);
        bool isInterface = (pAp == wld_rad_hostapd_getCfgMainVap(pAp->pRadio));
        wld_hostapdVapInfo_t* vapInfo = s_createHostapdVapInfo(isInterface, pAp->alias);
        if(vapInfo == NULL) {
            SAH_TRACEZ_ERROR(ME, "%s: fail to alloc vap conf section", pAp->alias);
            continue;
        }
        memcpy(&vapInfo->bssid, pAp->pSSID->MACAddress, SWL_MAC_BIN_LEN);
        if(isInterface) {
            amxc_llist_prepend(&config->vaps, &vapInfo->it);
        } else {
            amxc_llist_append(&config->vaps, &vapInfo->it);
        }
    }
    SAH_TRACEZ_OUT(ME);
    return true;
}

/**
 * @brief create wld_hostapd_config_t structure from Radio and Vaps configuration
 *
 * @param conf the structure mapping the content of hostapd configuration file
 * @param pRad radio ctx
 *
 * @return true on success.
 * Otherwise, false:
 *      - conf is NULL
 *      - Radio is NULL or has not vaps
 */
bool wld_hostapd_createConfig(wld_hostapd_config_t** pConf, T_Radio* pRad) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(pConf, false, ME, "NULL");
    ASSERTS_NOT_NULL(pRad, false, ME, "NULL");
    ASSERTS_FALSE(amxc_llist_is_empty(&pRad->llAP), false, ME, "No vaps");
    wld_hostapd_config_t* config = NULL;
    bool ret = s_initConfig(&config, &pRad->llAP);
    ASSERT_TRUE(ret, false, ME, "Bad config");
    *pConf = config;
    swl_mapChar_t* radConfigMap = wld_hostapd_getConfigMap(config, NULL);
    wld_hostapd_cfgFile_setRadioConfig(pRad, radConfigMap);

    /*
     * sort active vaps to be saved
     * 1- pure backhaul vaps
     * 2- other vaps
     */
    swl_unLiList_t aVaps;
    swl_unLiList_init(&aVaps, sizeof(T_AccessPoint*));
    T_AccessPoint* pTmpAp = NULL;
    wld_rad_forEachAp(pTmpAp, pRad) {
        if(wld_hostapd_ap_needWpaCtrlIface(pTmpAp)) {
            bool isPureBkh = SWL_BIT_IS_ONLY_SET(pTmpAp->multiAPType, MULTIAP_BACKHAUL_BSS);
            swl_unLiList_insert(&aVaps, isPureBkh - 1, &pTmpAp);
        }
    }

    /*
     * save vaps config:
     */
    swl_mapChar_t* defMultiAPConfig = NULL;
    swl_unLiListIt_t it;
    swl_unLiList_for_each(it, &aVaps) {
        if((pTmpAp = *(swl_unLiList_data(&it, T_AccessPoint * *))) != NULL) {
            swl_mapChar_t* multiAPConfig = defMultiAPConfig;
            swl_mapChar_t* vapConfigMap = wld_hostapd_getConfigMapByBssid(config, (swl_macBin_t*) pTmpAp->pSSID->BSSID);
            if(SWL_BIT_IS_ONLY_SET(pTmpAp->multiAPType, MULTIAP_BACKHAUL_BSS)) {
                defMultiAPConfig = defMultiAPConfig ? : vapConfigMap;
                multiAPConfig = vapConfigMap;
            }
            wld_hostapd_cfgFile_setVapConfig(pTmpAp, vapConfigMap, multiAPConfig);
        }
    }
    swl_unLiList_destroy(&aVaps);
    SAH_TRACEZ_OUT(ME);
    return true;
}

/**
 * @brief load the hostapd configuration from hostpad config file into wld_hostapd_config_t structure
 *
 * @param conf the structure mapping the content of hostapd configuration file
 * @param path the path of hostapd config file
 *
 * @return true on success.
 * Otherwise, false:
 *      - conf is NULL or path is NULL
 *      - config file not found
 *      - interface section is missing in the config file
 */
bool wld_hostapd_loadConfig(wld_hostapd_config_t** conf, char* path) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(conf, false, ME, "NULL");
    ASSERT_STR(path, false, ME, "Empty path");
    // Read the hostapd config file line by line and fill the config structure
    // Open the file
    FILE* fp = fopen(path, "r");
    ASSERTS_NOT_NULL(fp, false, ME, "NULL");

    wld_hostapd_config_t* config = calloc(1, sizeof(wld_hostapd_config_t));
    ASSERT_NOT_NULL(config, false, ME, "Config allocation of %s failed", path);
    *conf = config;

    char line[4096];
    char* pos;
    char* key;
    char* value;
    bool isHeaderParsing = true;
    bool ret = true;
    wld_hostapdVapInfo_t* lastVap = NULL;

    // Init the header map
    swl_mapChar_init(&(config->header));

    while(fgets(line, sizeof(line), fp) != NULL) {
        if(line[0] == '#') {
            continue;
        }

        pos = strchr(line, '\n');
        if(pos != NULL) {
            *pos = '\0';
        }

        pos = strchr(line, '=');
        if(pos == NULL) {
            SAH_TRACEZ_ERROR(ME, "%s: invalid line %s", path, line);
            continue;
        }
        *pos = '\0';

        key = line;
        value = pos + 1;

        if(swl_str_matches(key, "interface")) {
            isHeaderParsing = false;
            amxc_llist_init(&config->vaps);
            lastVap = s_createHostapdVapInfo(true, value);
            if(lastVap == NULL) {
                ret = false;
                break;
            }
            amxc_llist_append(&config->vaps, &lastVap->it);
        } else if(swl_str_matches(key, "bss")) {
            lastVap = s_createHostapdVapInfo(false, value);
            if(lastVap == NULL) {
                ret = false;
                break;
            }
            amxc_llist_append(&config->vaps, &lastVap->it);
        }
        if(isHeaderParsing) {
            swl_mapChar_add(&(config->header), key, value);
        } else {
            swl_mapChar_add(&(lastVap->vapParams), key, value);
            if(swl_str_matches(key, "bssid")) {
                swl_typeMacBin_fromChar(&lastVap->bssid, value);
            }
        }
    }
    fclose(fp);
    // error or interface section is missing in the config file
    if((!ret) || (isHeaderParsing)) {
        SAH_TRACEZ_ERROR(ME, "%s: %s", path, isHeaderParsing ? "interface section is missing" : "error when creating vapInfo");
        wld_hostapd_deleteConfig(config);
        config = NULL;
    }
    SAH_TRACEZ_OUT(ME);
    return (ret && !isHeaderParsing);
}

/**
 * @brief delete a wld_hostapd_config_t map already loaded from hostapd file
 *
 * @param conf the structure mapping the content of hostapd configuration file
 *
 * @return true on success.
 * Otherwise, false
 *      - conf is NULL
 */
bool wld_hostapd_deleteConfig(wld_hostapd_config_t* conf) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(conf, false, ME, "NULL");
    swl_mapChar_cleanup(&(conf->header));
    amxc_llist_it_t* it = amxc_llist_get_first(&conf->vaps);
    while(it) {
        wld_hostapdVapInfo_t* vapInfo = amxc_llist_it_get_data(it, wld_hostapdVapInfo_t, it);
        it = amxc_llist_it_get_next(it);
        s_deleteHostapdVapInfo(vapInfo);
    }
    free(conf);
    SAH_TRACEZ_OUT(ME);
    return true;
}

/**
 * @brief write the content of hostapd_config into a file
 *
 * @param conf the structure mapping the content of hostapd configuration file
 * @param path the filename where the conf is written
 *
 * @return true if the conf map is successfully written in the file
 * Otherwise, false:
 *      - conf is NULL or Path is NULL
 *      - if writing of the conf in the file "path" failed
 */
bool wld_hostapd_writeConfig(wld_hostapd_config_t* conf, char* path) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(conf, false, ME, "NULL");
    ASSERT_STR(path, false, ME, "Empty path");
    char tmpName[128];
    bool ret;

    // Write header into temporary file
    snprintf(tmpName, sizeof(tmpName), "%s.tmp.txt", path);
    FILE* fptr = fopen(tmpName, "w");
    ASSERTS_NOT_NULL(fptr, false, ME, "NULL");
    fprintf(fptr, "## General configurations\n");
    ret = swl_mapWriterKVP_writeToFptrEsc(&(conf->header), fptr, "\\", '\\');
    if(!ret) {
        SAH_TRACEZ_ERROR(ME, "writing config header failed");
        fclose(fptr);
        unlink(tmpName);
        return false;
    }

    // Write vaps list into into temporary file
    amxc_llist_for_each(it, &conf->vaps) {
        wld_hostapdVapInfo_t* vapInfo = amxc_llist_it_get_data(it, wld_hostapdVapInfo_t, it);
        if(vapInfo->isInterface) {
            fprintf(fptr, "## Interface configurations\n");
        } else {
            fprintf(fptr, "## BSS configurations\n");
        }
        ret = swl_mapWriterKVP_writeToFptrEsc(&(vapInfo->vapParams), fptr, "\\", '\\');
        if(!ret) {
            SAH_TRACEZ_ERROR(ME, "writing config header failed");
            break;
        }
    }

    fclose(fptr);
    // Rename the file
    if(ret) {
        ret = rename(tmpName, path) >= 0 ? true : false;
    }
    unlink(tmpName);

    SAH_TRACEZ_OUT(ME);
    return ret;
}

/**
 * @brief add/update a key value in the config map
 *
 * @param conf the structure mapping the content of hostapd configuration file
 * @param bssName if NULL then the key to set is in the header part (general config). Otherwise, the key is in the vaps part
 * @param key the key to add/update
 * @param key the new key value
 *
 * @return true if the parameter is added/updated.
 * Otherwise false:
 *      - conf = NULL or key = NULL or value = NULL
 *      - the bssName doesn't exist in the config file
 */
bool wld_hostapd_addConfigParam(wld_hostapd_config_t* conf, const char* bssName, const char* key, const char* value) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(conf, false, ME, "NULL");
    ASSERTS_NOT_NULL(key, false, ME, "NULL");
    ASSERTS_NOT_NULL(value, false, ME, "NULL");
    bool ret = false;

    if(bssName == NULL) {
        ret = swl_map_addOrSet(&conf->header, (char*) key, (char*) value);
    } else {
        amxc_llist_for_each(it, &conf->vaps) {
            wld_hostapdVapInfo_t* vapInfo = amxc_llist_it_get_data(it, wld_hostapdVapInfo_t, it);
            if(swl_str_matches(vapInfo->bssName, bssName)) {
                ret = swl_map_addOrSet(&vapInfo->vapParams, (char*) key, (char*) value);
                break;
            }
        }
    }
    SAH_TRACEZ_OUT(ME);
    return ret;
}


/**
 * @brief delete a key/value from the config map
 *
 * @param conf the structure mapping the content of hostapd configuration file
 * @param bssName if NULL then the key to set is in the header part (general config). Otherwise, the key is in the vaps part
 * @param key the key to delete
 *
 * @return true if the parameter is deleted.
 * Otherwise false:
 *      - conf = NULL or key = NULL or value = NULL
 *      - the bssName doesn't exist in the config file
 *      - key is NULL or doesn't exist
 */
bool wld_hostapd_delConfigParam(wld_hostapd_config_t* conf, char* bssName, char* key) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(conf, false, ME, "NULL");
    ASSERTS_NOT_NULL(key, false, ME, "NULL");
    bool ret = false;

    if(bssName == NULL) {
        ret = swl_map_has(&conf->header, key);
        ASSERT_TRUE(ret, false, ME, "key %s doesn't exist", key);
        swl_map_delete(&conf->header, key);
    } else {
        amxc_llist_for_each(it, &conf->vaps) {
            wld_hostapdVapInfo_t* vapInfo = amxc_llist_it_get_data(it, wld_hostapdVapInfo_t, it);
            if(swl_str_matches(vapInfo->bssName, bssName)) {
                ret = swl_map_has(&vapInfo->vapParams, key);
                ASSERT_TRUE(ret, false, ME, "key %s doesn't exist", key);
                swl_map_delete(&vapInfo->vapParams, key);
                break;
            }
        }
    }
    SAH_TRACEZ_OUT(ME);
    return ret;
}

/**
 * @brief get the general/vap config map
 *
 * @param conf the structure mapping the content of hostapd configuration file
 * @param bssName if NULL then the key to set is in the header part (general config). Otherwise, the key is in the vaps part
 *
 * @return a pointer to the appropriate config map
 */
swl_mapChar_t* wld_hostapd_getConfigMap(wld_hostapd_config_t* conf, char* bssName) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(conf, NULL, ME, "NULL");
    if(bssName == NULL) {
        return &(conf->header);
    } else {
        amxc_llist_for_each(it, &conf->vaps) {
            wld_hostapdVapInfo_t* vapInfo = amxc_llist_it_get_data(it, wld_hostapdVapInfo_t, it);
            if(swl_str_matches(vapInfo->bssName, bssName)) {
                return &(vapInfo->vapParams);
            }
        }
    }
    SAH_TRACEZ_OUT(ME);
    return NULL;
}

const char* wld_hostapd_getConfigParamValStr(wld_hostapd_config_t* conf, char* bssName, const char* key) {
    swl_mapChar_t* configMap = wld_hostapd_getConfigMap(conf, bssName);
    ASSERTS_NOT_NULL(configMap, NULL, ME, "NULL");
    swl_mapEntry_t* entry = swl_mapChar_getEntry(configMap, (char*) key);
    ASSERTI_NOT_NULL(entry, NULL, ME, "key (%s) Not found", key);
    return swl_map_getEntryValueValue(configMap, entry);
}

swl_mapChar_t* wld_hostapd_getConfigMapByBssid(wld_hostapd_config_t* conf, swl_macBin_t* bssid) {
    SAH_TRACEZ_IN(ME);
    ASSERTS_NOT_NULL(conf, NULL, ME, "NULL");
    if(bssid == NULL) {
        return &(conf->header);
    } else {
        amxc_llist_for_each(it, &conf->vaps) {
            wld_hostapdVapInfo_t* vapInfo = amxc_llist_it_get_data(it, wld_hostapdVapInfo_t, it);
            if(swl_typeMacBin_equalsRef(&vapInfo->bssid, bssid)) {
                return &(vapInfo->vapParams);
            }
        }
    }
    SAH_TRACEZ_OUT(ME);
    return NULL;
}

const char* wld_hostapd_getConfigParamByBssidValStr(wld_hostapd_config_t* conf, swl_macBin_t* bssid, const char* key) {
    swl_mapChar_t* configMap = wld_hostapd_getConfigMapByBssid(conf, bssid);
    ASSERTS_NOT_NULL(configMap, NULL, ME, "NULL");
    swl_mapEntry_t* entry = swl_mapChar_getEntry(configMap, (char*) key);
    ASSERTI_NOT_NULL(entry, NULL, ME, "key (%s) Not found", key);
    return swl_map_getEntryValueValue(configMap, entry);
}


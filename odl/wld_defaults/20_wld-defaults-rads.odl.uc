%populate {
    object WiFi {
        object Radio {
{% let RadioId = 0 %}
{% for ( let Radio in BD.Radios ) : %}
{% RadioId++ %}
{% if (Radio.OperatingFrequency == "2.4GHz") : %}
            instance add ("{{Radio.Alias}}") {
                parameter OperatingFrequencyBand = "2.4GHz";
                parameter Enable = 1;
                parameter RegulatoryDomain = "DE";
                parameter AP_Mode = 1;
                parameter AirtimeFairnessEnabled = 1;
                parameter IntelligentAirtimeSchedulingEnable = 1;
                parameter RxPowerSaveEnabled = 1;
                parameter RetryLimit = 7;
                parameter TargetWakeTimeEnable = 1;
                parameter ObssCoexistenceEnable = 1;
                object MACConfig {
                    parameter UseBaseMacOffset = false;
                    parameter UseLocalBitForGuest = true;
                }
            }
{% elif (Radio.OperatingFrequency == "5GHz") : %}
            instance add ("{{Radio.Alias}}") {
                parameter OperatingFrequencyBand = "5GHz";
                parameter Enable = 1;
                parameter Channel = 36;
                parameter IEEE80211hEnabled = true;
                parameter RegulatoryDomain = "DE";
                parameter AP_Mode = 1;
                parameter WDS_Mode = 1;
                parameter AirtimeFairnessEnabled = 1;
                parameter IntelligentAirtimeSchedulingEnable = 1;
                parameter MultiUserMIMOEnabled = 1;
                parameter RxPowerSaveEnabled = 1;
                parameter RetryLimit = 7;
                parameter TargetWakeTimeEnable = 1;
                object RadCaps {
                    parameter Enabled = "DFS_AHEAD DELAY_COMMIT";
                }
                parameter AutoBandwidthSelectMode="MaxCleared";
                object MACConfig {
                    parameter UseBaseMacOffset = false;
                    parameter UseLocalBitForGuest = true;
                }
                object ChannelMgt {
                    object BgDfs {
                        parameter PreclearEnable = true;
                    }
                }
            }
{% elif (Radio.OperatingFrequency == "6GHz") : %}
            instance add ("{{Radio.Alias}}") {
                parameter OperatingFrequencyBand = "6GHz";
                parameter Enable = 1;
                parameter RegulatoryDomain = "DE";
                parameter AP_Mode = 1;
                parameter WDS_Mode = 1;
                parameter AirtimeFairnessEnabled = 1;
                parameter IntelligentAirtimeSchedulingEnable = 1;
                parameter MultiUserMIMOEnabled = 1;
                parameter RxPowerSaveEnabled = 1;
                parameter RetryLimit = 7;
                parameter HeCapsEnabled = "DL_OFDMA,UL_OFDMA,DL_MUMIMO";
                parameter TargetWakeTimeEnable = 1;
                object RadCaps {
                    parameter Enabled = "DFS_AHEAD DELAY_COMMIT";
                }
                parameter AutoBandwidthSelectMode="MaxAvailable";
                object MACConfig {
                    parameter UseBaseMacOffset = false;
                    parameter UseLocalBitForGuest = false;
                }
            }
{% endif; endfor; %}
        }
    }
}

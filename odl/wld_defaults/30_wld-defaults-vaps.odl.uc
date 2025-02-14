%populate {
    object WiFi {
        object SSID {
{% let apindex = 0 %}
{% for ( let Itf in BD.Interfaces ) : if ( BDfn.isInterfaceWirelessAp(Itf.Name) ) : %}
{% if (BDfn.isInterfaceLan(Itf.Name)) : %}
{% if (Itf.OperatingFrequency != "") : %}
{% apindex = BDfn.getInterfaceIndex(Itf.Name, "wireless") %}
            instance add ({{apindex + 1}}, "{{Itf.Alias}}") {
{% if ( Itf.SSID != "" ) : %}
                parameter SSID = "{{Itf.SSID}}";
{% endif %}
                parameter Enable = 0;
{% RadioIndex = BDfn.getRadioIndex(Itf.OperatingFrequency);
if (RadioIndex >= 0) : %}
                parameter LowerLayers = "Device.WiFi.Radio.{{RadioIndex + 1}}.";
{% endif %}
            }
{% endif %}
{% endif %}
{% endif; endfor; %}
        }
        object AccessPoint {
{% let apindex = 0 %}
{% for ( let Itf in BD.Interfaces ) : if ( BDfn.isInterfaceWirelessAp(Itf.Name) ) : %}
{% if (BDfn.isInterfaceLan(Itf.Name)) : %}
{% if (Itf.OperatingFrequency != "") : %}
{% apindex = BDfn.getInterfaceIndex(Itf.Name, "wireless")  %}
{% if ( Itf.SSID != "" ) : %}
            instance add ("{{Itf.Alias}}") {
                parameter SSIDReference = "Device.WiFi.SSID.{{apindex + 1}}.";
{% if (BDfn.isInterfaceGuest(Itf.Name)) : %}
                parameter DefaultDeviceType = "Guest";
                parameter Enable = 0;
{% elif (BDfn.isInterfaceLan(Itf.Name)) : %}
                parameter IEEE80211kEnabled = 1;
                parameter MultiAPType = "FronthaulBSS,BackhaulBSS";
                parameter Enable = 1;
{% if ((Itf.OperatingFrequency == "5GHz") || (Itf.OperatingFrequency == "6GHz")) : %}
                parameter WDSEnable = 1;
{% endif %}
{% endif %}
                object Security {
                    parameter RekeyingInterval = 3600;
{% if (Itf.OperatingFrequency == "6GHz") : %}
                    parameter ModesAvailable = "WPA3-Personal";
                    parameter ModeEnabled = "WPA3-Personal";
                    parameter SAEPassphrase = "";
                    parameter SPPAmsdu = 0;
{% else %}
                    parameter ModesAvailable = "None,WPA2-Personal,WPA3-Personal,WPA2-WPA3-Personal,OWE";
                    parameter ModeEnabled = "WPA2-Personal";
{% endif %}
{% if (BDfn.isInterfaceGuest(Itf.Name)) : %}
                    parameter KeyPassPhrase = "passwordGuest";
{% else %}
                    parameter KeyPassPhrase = "password";
{% endif %}
                }
                object WPS {
                    parameter ConfigMethodsEnabled = "PhysicalPushButton,VirtualPushButton,Display,VirtualDisplay,PIN";
                    parameter Configured = 1;
{% if (BDfn.isInterfaceLan(Itf.Name) && (Itf.OperatingFrequency != "6GHz")) : %}
                    parameter Enable = 1;
{% endif %}
                }
            }
{% endif %}
{% endif %}
{% endif %}
{% endif; endfor; %}
        }
    }
}

//
//  WatchWinder.cpp - Library for WatchWinder based on ESP8266.
//  Created by Jonathan Bryan Schmalhofer, March 4th 2018
//  License: MIT
//

#include "WatchWinder.h"

WatchWinder::WatchWinder()
    : wifi_manager_(),
      web_server_(80),
      save_config_(true)
{
    strcpy(mqtt_port_, "8080");
    strcpy(blynk_token_, "YOUR_BLYNK_TOKEN");

    strcpy(static_ip_, "10.0.1.56");
    strcpy(static_gateway_, "10.0.1.1");
    strcpy(static_subnet_, "255.255.255.0");

    ApplyTimesettings();
}

void WatchWinder::Setup()
{
    Serial.println("###################################################");
    Serial.println("### Running WatchWinder Setup");

    SetupWifiManager();
    SetupMovement();
    SetupNTPClient();
    
    timesettings_.Load();
    timesettings_.Info();
    ApplyTimesettings();
    
    SetupWatchMovementSuppliers();

    // HTML
    web_server_.on("/", (std::bind(&WatchWinder::HandleRoot, this)));
    web_server_.on("/index.html", (std::bind(&WatchWinder::HandleRoot, this)));
    web_server_.on("/watches.html", (std::bind(&WatchWinder::HandleWatchesHTML, this)));
    web_server_.on("/settings.html", (std::bind(&WatchWinder::HandleSettingsHTML, this)));
    web_server_.on("/info.html", (std::bind(&WatchWinder::HandleInfoHTML, this)));
    
    // JS
    web_server_.on("/js/timesettings.js", (std::bind(&WatchWinder::HandleTimesettingsJS, this)));
    web_server_.on("/js/watches.js", (std::bind(&WatchWinder::HandleWatchesJS, this)));
    web_server_.on("/js/functions.js", (std::bind(&WatchWinder::HandleFunctionsJS, this)));
    
    // CSS
    web_server_.on("/style.css", (std::bind(&WatchWinder::HandleStyleCSS, this)));
    
    // JSON
    web_server_.on("/timesettings.json", (std::bind(&WatchWinder::HandleTimesettingsJSON, this)));
    web_server_.on("/timesettingsSave.json", (std::bind(&WatchWinder::HandleTimesettingsSaveJSON, this)));
    web_server_.on("/timesettingsReset.json", (std::bind(&WatchWinder::HandleTimesettingsResetJSON, this)));
    web_server_.on("/watches.json", (std::bind(&WatchWinder::HandleWatchesJSON, this)));
    web_server_.on("/watchesSave.json", (std::bind(&WatchWinder::HandleWatchesSaveJSON, this)));
    web_server_.on("/watchesReset.json", (std::bind(&WatchWinder::HandleWatchesResetJSON, this)));
    web_server_.on("/restartESP.json", (std::bind(&WatchWinder::HandleRestartESPJSON, this)));

    web_server_.begin();

    Serial.println("HTTP server started");
}

void WatchWinder::SetupWatchMovementSuppliers()
{
    // In my current design, there is only one stepper connected to the ESP8266
    WatchMovementSupplier stepper_providing_movement;

    const short kdesired_watch_rpm   = 19;  // According to the following link, watch winders like the Elma Schmidbauer GmbH Cyclomatic Due™ or Buben-Zorweg Time Mover™ rotate at 19pm (for 32mins) so "a watch can be given 24 hours' worth of power reserve"
                                            // we will thus also use this as default value
    const short kpinion_number_teeth = 23;  // In my current design, the pinion has 23 teeth
    const short kgear_number_teeth   = 55;  // In my current design, the gears (all three equal) have 55 teeth
    const short kdefault_rpm = (short)(kgear_number_teeth/kpinion_number_teeth * kdesired_watch_rpm);
                                            // the stepper itself needs to run (approx 2.39 times) faster to achieve the 19 rpm for the watches
    stepper_providing_movement.SetRPM(kdefault_rpm);

    // First Watch - next to stepper
    WatchRequirementTurningLikeStepper::first_type first_watch_requirements("First Watch", BOTHDIRECTIONS, 720); // 720 seems to be a good value for "turns per day" according to http://people.timezone.com/msandler/Articles/ArnsteinWinder/Winder2.html
    WatchRequirementTurningLikeStepper::second_type first_watch_turning_like_stepper = false;
    WatchRequirementTurningLikeStepper first_watch = std::make_pair(first_watch_requirements, first_watch_turning_like_stepper);

    // Second Watch - in the middle
    WatchRequirementTurningLikeStepper::first_type second_watch_requirements("Second Watch", BOTHDIRECTIONS, 720); // 720 seems to be a good value for "turns per day" according to http://people.timezone.com/msandler/Articles/ArnsteinWinder/Winder2.html
    WatchRequirementTurningLikeStepper::second_type second_watch_turning_like_stepper = true;
    WatchRequirementTurningLikeStepper second_watch = std::make_pair(second_watch_requirements, second_watch_turning_like_stepper);

    // Third Watch - at the opposite end of the stepper
    WatchRequirementTurningLikeStepper::first_type third_watch_requirements("Third Watch", BOTHDIRECTIONS, 720); // 720 seems to be a good value for "turns per day" according to http://people.timezone.com/msandler/Articles/ArnsteinWinder/Winder2.html
    WatchRequirementTurningLikeStepper::second_type third_watch_turning_like_stepper = false;
    WatchRequirementTurningLikeStepper third_watch = std::make_pair(third_watch_requirements, third_watch_turning_like_stepper);

    // In my current design, there are three watches attached to the stepper imposing three requirements (one per watch) on the movement to be performed by the stepper per day
    stepper_providing_movement.GetAllRequirements().push_back(first_watch);
    stepper_providing_movement.GetAllRequirements().push_back(second_watch);
    stepper_providing_movement.GetAllRequirements().push_back(third_watch);

    watch_movement_suppliers_.push_back(stepper_providing_movement);
}

void WatchWinder::SendFile(int code, String type, const char* adr, size_t len)
{
    SendHeader(code,type,len);
    web_server_.sendContent_P(adr,len);
    SendBuffer();
}

void WatchWinder::SendHeader(int code, String type, size_t _size)
{
    web_server_.setContentLength(_size);
    web_server_.send(code, type, "");
}

void WatchWinder::SendBuffer()
{
    if(buffer_counter_ > 0)
    {
        web_server_.sendContent_P(data_website_buffer_, buffer_counter_);
        buffer_counter_ = 0;
    }
}

void WatchWinder::SendToBuffer(String str)
{
    size_t length = str.length();
    if(buffer_counter_ + length > BUFFER_SIZE)
    {
        web_server_.sendContent_P(data_website_buffer_, buffer_counter_);
        buffer_counter_ = 0;
    }
    memcpy(data_website_buffer_ + buffer_counter_, str.c_str(), length);
    buffer_counter_ += length;
}

void WatchWinder::HandleRoot()
{
    HandleWatchesHTML();
}

void WatchWinder::HandleSettingsHTML()
{
    SendFile(200, "text/html", data_settingsHTML, sizeof(data_settingsHTML));
}

void WatchWinder::HandleWatchesHTML()
{
    SendFile(200, "text/html", data_watchesHTML, sizeof(data_watchesHTML));
}

void WatchWinder::HandleInfoHTML()
{
    SendFile(200, "text/html", data_infoHTML, sizeof(data_infoHTML));
}

void WatchWinder::HandleTimesettingsJS()
{
    SendFile(200, "text/javascript", data_js_timesettingsJS, sizeof(data_js_timesettingsJS));
}

void WatchWinder::HandleWatchesJS()
{
    SendFile(200, "text/javascript", data_js_watchesJS, sizeof(data_js_watchesJS));
}

void WatchWinder::HandleFunctionsJS()
{
    SendFile(200, "text/javascript", data_js_functionsJS, sizeof(data_js_functionsJS));
}

void WatchWinder::HandleStyleCSS()
{
    SendFile(200, "text/css;charset=UTF-8", data_styleCSS, sizeof(data_styleCSS));
}

void WatchWinder::HandleTimesettingsJSON()
{
    SendHeader(200, "text/json", timesettings_.GetSize());
    String json = timesettings_.GetTimesettingsJSON();
    SendToBuffer(json);
    SendBuffer();
}

void WatchWinder::HandleTimesettingsSaveJSON()
{
    if (web_server_.hasArg("timezoneshift"))
    {
        timesettings_.SetTimezoneshift(web_server_.arg("timezoneshift").toInt());
    }
    if (web_server_.hasArg("earliestallowed"))
    {
        timesettings_.SetEarliestallowed(web_server_.arg("earliestallowed").toInt());
    }
    if (web_server_.hasArg("latestallowed"))
    {
        timesettings_.SetLatestallowed(web_server_.arg("latestallowed").toInt());
    }
    timesettings_.Save();
    ApplyTimesettings();
    web_server_.send(200, "text/json", "true");
}

void WatchWinder::HandleTimesettingsResetJSON()
{
    timesettings_.Reset();
    ApplyTimesettings();
    web_server_.send(200, "text/json", "true");
}

void WatchWinder::HandleWatchesJSON()
{
    String json = GetWatchesJSON();
    size_t json_size = json.length();
    SendHeader(200, "text/json", json_size);
    SendToBuffer(json);
    SendBuffer();
}

void WatchWinder::HandleWatchesSaveJSON()
{
	if (web_server_.hasArg("firstwatchname"))
    {
		SetFirstwatchname(web_server_.arg("firstwatchname"));
    }
    if (web_server_.hasArg("firstwatchturnsperday"))
    {
		SetFirstwatchturnsperday(web_server_.arg("firstwatchturnsperday").toInt());
    }
    if (web_server_.hasArg("firstwatchturndirection"))
    {
		SetFirstwatchturndirection((TurningDirection)(web_server_.arg("firstwatchturndirection").toInt()));
    }
	if (web_server_.hasArg("secondwatchname"))
    {
		SetSecondwatchname(web_server_.arg("secondwatchname"));
    }
    if (web_server_.hasArg("secondwatchturnsperday"))
    {
		SetSecondwatchturnsperday(web_server_.arg("secondwatchturnsperday").toInt());
    }
    if (web_server_.hasArg("secondwatchturndirection"))
    {
		SetSecondwatchturndirection((TurningDirection)(web_server_.arg("secondwatchturndirection").toInt()));
    }
	if (web_server_.hasArg("thirdwatchname"))
    {
		SetThirdwatchname(web_server_.arg("thirdwatchname"));
    }
    if (web_server_.hasArg("thirdwatchturnsperday"))
    {
		SetThirdwatchturnsperday(web_server_.arg("thirdwatchturnsperday").toInt());
    }
    if (web_server_.hasArg("thirdwatchturndirection"))
    {
		SetThirdwatchturndirection((TurningDirection)(web_server_.arg("thirdwatchturndirection").toInt()));
    }
	web_server_.send(200, "text/json", "true");
}

void WatchWinder::HandleWatchesResetJSON()
{
	SetFirstwatchname("First Watch");
    SetFirstwatchturnsperday(720);
    SetFirstwatchturndirection(BOTHDIRECTIONS);

    SetSecondwatchname("Second Watch");
    SetSecondwatchturnsperday(720);
    SetSecondwatchturndirection(BOTHDIRECTIONS);

    SetThirdwatchname("Third Watch");
    SetThirdwatchturnsperday(720);
    SetThirdwatchturndirection(BOTHDIRECTIONS);
	web_server_.send(200, "text/json", "true");
}

void WatchWinder::HandleRestartESPJSON()
{
    web_server_.send(200, "text/json", "true");
    ESP.restart();
}

void WatchWinder::ReadConfig()
{
    if (SPIFFS.begin())
    {
        if (SPIFFS.exists("/config.json"))
        {
          if (SPIFFS.remove("/config.json"))
          {
            Serial.println("Deleted /config.json");
          }
        }
        if (SPIFFS.exists("/config.json"))
        {
        //file exists, reading and loading
        File configFile = SPIFFS.open("/config.json", "r");
        if (configFile)
        {
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                DynamicJsonBuffer jsonBuffer;
                JsonObject& json = jsonBuffer.parseObject(buf.get());
                json.printTo(Serial);
                if (json.success())
                {
                    strcpy(mqtt_server_, json["mqtt_server"]);
                    strcpy(mqtt_port_, json["mqtt_port"]);
                    strcpy(blynk_token_, json["blynk_token"]);

                    if(json["ip"])
                    {
                        strcpy(static_ip_, json["ip"]);
                        strcpy(static_gateway_, json["gateway"]);
                        strcpy(static_subnet_, json["subnet"]);
                    }
                    else
                    {
                        Serial.println("no custom ip in config");
                    }
                }
                else
                {
                    Serial.println("failed to load json config");
                }
            }
        }
    }
    else
    {
        Serial.println("failed to mount FS");
    }
}

void WatchWinder::SetupWifiManager()
{
    ReadConfig();

    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server_, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port_, 6);
    WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token_, 32);
    
    //set config save notify callback
    //TODO:wifi_manager_.setSaveConfigCallback((std::bind(&WatchWinder::SaveConfigCallback, this)));

    // set static ip
    //wifi_manager_.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

    // add all your parameters here
    wifi_manager_.addParameter(&custom_mqtt_server);
    wifi_manager_.addParameter(&custom_mqtt_port);
    wifi_manager_.addParameter(&custom_blynk_token);

    // reset settings - for testing
    //wifi_manager_.resetSettings();

    // set minimu quality of signal so it ignores AP's under that quality
    // defaults to 8%
    //wifi_manager_.setMinimumSignalQuality();

    // sets timeout until configuration portal gets turned off
    // useful to make it all retry or go to sleep
    // in seconds
    wifi_manager_.setTimeout(120);

    // fetches ssid and pass and tries to connect
    // if it does not connect it starts an access point with the specified name
    // here  "WatchWinderESP8266"
    // and goes into a blocking loop awaiting configuration
    if (!wifi_manager_.autoConnect("WatchWinderESP8266", "watchwinder")) // first parameter is name of access point, second is the password
    {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        // reset and try again, or maybe put it to deep sleep
        wifi_manager_.resetSettings();
        Serial.println("resetting wifi manager and restarting ESP8266");
        ESP.restart();
        delay(5000);
    }

    // if you get here you have connected to the WiFi
    Serial.println("connected...");

    // read updated parameters
    strcpy(mqtt_server_, custom_mqtt_server.getValue());
    strcpy(mqtt_port_, custom_mqtt_port.getValue());
    strcpy(blynk_token_, custom_blynk_token.getValue());

    // save the custom parameters to FS
    if (save_config_)
    {
        Serial.println("saving config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
        json["mqtt_server"] = mqtt_server_;
        json["mqtt_port"] = mqtt_port_;
        json["blynk_token"] = blynk_token_;

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile)
        {
            Serial.println("failed to open config file for writing");
        }

        json.printTo(Serial);
        json.printTo(configFile);
        configFile.close();
        // end save
    }

    Serial.println("local ip");
    Serial.println(WiFi.localIP());

}

void WatchWinder::SetupMovement()
{
}

void WatchWinder::SetupNTPClient()
{
    NTP.init((char *)"de.pool.ntp.org", UTC0100); // hardcoded: German NTP Server, Central European Time (CET = UTC + 01:00)
    NTP.setPollingInterval(60*5); // Poll every 5 minutes

    NTP.onSyncEvent([](NTPSyncEvent_t ntpEvent)
    {
        switch (ntpEvent)
        {
            case NTP_EVENT_INIT:
                break;
            case NTP_EVENT_STOP:
                break;
            case NTP_EVENT_NO_RESPONSE:
                Serial.printf("NTP server not reachable.\n");
                break;
            case NTP_EVENT_SYNCHRONIZED:
                Serial.printf("Got NTP time: %s\n", NTP.getTimeDate(NTP.getLastSync()));
            break;
        }
    });
}
void WatchWinder::SaveConfigCallback()
{
    Serial.println("Should save config");
    save_config_ = true;
}

void WatchWinder::Step()
{
    if( InAllowedTimeFrameOfDay() )
    {
        Serial.println("Time is ok");
        for (auto& watch_movement : watch_movement_suppliers_)
        {
        }
    }
    web_server_.handleClient();
}

bool WatchWinder::InAllowedTimeFrameOfDay()
{
    if( hour(now()) >= hour(earliest_allowed_movement_) &&
        hour(now()) <= hour(latest_allowed_movement_) )
    {
        return true;
    }
    else
    {
        return false;
    }
}

std::vector<WatchMovementSupplier> WatchWinder::GetAllWatchMovementSuppliers()
{
    return watch_movement_suppliers_;
}

void WatchWinder::AddWatchMovementSupplier()
{
    watch_movement_suppliers_.push_back(WatchMovementSupplier());
}

time_t WatchWinder::ConvertEpochHourToUnixTimestamp(int hour, int relative_to_gmt)
{
    struct tm dummy_time = {0};  // Initalize to all 0's
    dummy_time.tm_year = 0;    // This is year-1900, so 118 = 2018
    dummy_time.tm_mon  = 1;
    dummy_time.tm_mday = 1;
    dummy_time.tm_hour = hour - relative_to_gmt * 60 * 60;
    dummy_time.tm_min  = 0;
    dummy_time.tm_sec  = 0;

    return mktime(&dummy_time);
}

void WatchWinder::ApplyTimesettings()
{
    earliest_allowed_movement_ = ConvertEpochHourToUnixTimestamp(timesettings_.GetEarliestallowed(), timesettings_.GetTimezoneshift());
    latest_allowed_movement_   = ConvertEpochHourToUnixTimestamp(timesettings_.GetLatestallowed(),   timesettings_.GetTimezoneshift());
}

String WatchWinder::GetWatchesJSON()
{
	
	String json = "{";
    json += "\"firstwatchname\":\""   		  + (String)GetFirstwatchname() 			+ "\",";
    json += "\"firstwatchturnsperday\":" 	  + (String)GetFirstwatchturnsperday() 	    + ",";
    json += "\"firstwatchturndirection\":\""  + (String)GetFirstwatchturndirection() 	+ "\",";
    json += "\"secondwatchname\":\""   		  + (String)GetSecondwatchname() 			+ "\",";
    json += "\"secondwatchturnsperday\":" 	  + (String)GetSecondwatchturnsperday() 	+ ",";
    json += "\"secondwatchturndirection\":\"" + (String)GetSecondwatchturndirection()   + "\",";
    json += "\"thirdwatchname\":\""   		  + (String)GetThirdwatchname() 			+ "\",";
    json += "\"thirdwatchturnsperday\":" 	  + (String)GetThirdwatchturnsperday() 	    + ",";
    json += "\"thirdwatchturndirection\":\""  + (String)GetThirdwatchturndirection() 	+ "\"}";
    
    return json;
}

String WatchWinder::GetFirstwatchname()
{
	String empty_name = "First Watch";
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 0)
		{
			return (String)(watch_movement_suppliers_.at(0).GetAllRequirements().at(0).first.GetName());
		}
	}
	return empty_name;
}

int WatchWinder::GetFirstwatchturnsperday()
{
	int revolutions_per_day = 720; // Default
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 0)
		{
			return (int)(watch_movement_suppliers_.at(0).GetAllRequirements().at(0).first.GetRevolutionsPerDay());
		}
	}
	return revolutions_per_day;
}

TurningDirection WatchWinder::GetFirstwatchturndirection()
{
	TurningDirection turning_direction = BOTHDIRECTIONS; // Default
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 0)
		{
			return (TurningDirection)(watch_movement_suppliers_.at(0).GetAllRequirements().at(0).first.GetTurningDirection());
		}
	}
	return turning_direction;
}

String WatchWinder::GetSecondwatchname()
{
	String empty_name = "Second Watch";
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 1)
		{
			return (String)(watch_movement_suppliers_.at(0).GetAllRequirements().at(1).first.GetName());
		}
	}
	return empty_name;
}

int WatchWinder::GetSecondwatchturnsperday()
{
	int revolutions_per_day = 720; // Default
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 1)
		{
			return (int)(watch_movement_suppliers_.at(0).GetAllRequirements().at(1).first.GetRevolutionsPerDay());
		}
	}
	return revolutions_per_day;
}

TurningDirection WatchWinder::GetSecondwatchturndirection()
{
	TurningDirection turning_direction = BOTHDIRECTIONS; // Default
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 1)
		{
			return (TurningDirection)(watch_movement_suppliers_.at(0).GetAllRequirements().at(1).first.GetTurningDirection());
		}
	}
	return turning_direction;
}

String WatchWinder::GetThirdwatchname()
{
	String empty_name = "Third Watch";
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 2)
		{
			return (String)(watch_movement_suppliers_.at(0).GetAllRequirements().at(2).first.GetName());
		}
	}
	return empty_name;
}

int WatchWinder::GetThirdwatchturnsperday()
{
	int revolutions_per_day = 720; // Default
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 2)
		{
			return (int)(watch_movement_suppliers_.at(0).GetAllRequirements().at(2).first.GetRevolutionsPerDay());
		}
	}
	return revolutions_per_day;
}

TurningDirection WatchWinder::GetThirdwatchturndirection()
{
	TurningDirection turning_direction = BOTHDIRECTIONS; // Default
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 2)
		{
			return (TurningDirection)(watch_movement_suppliers_.at(0).GetAllRequirements().at(2).first.GetTurningDirection());
		}
	}
	return turning_direction;
}

void WatchWinder::SetFirstwatchname(String name)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 0)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(0).first.SetName(name);
			Serial.println("SetFirstwatchname() SUCCESS");
		}
	}
}

void WatchWinder::SetFirstwatchturnsperday(int revolutions_per_day)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 0)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(0).first.SetRevolutionsPerDay(revolutions_per_day);
			Serial.println("SetFirstwatchturnsperday() SUCCESS");
		}
	}
}

void WatchWinder::SetFirstwatchturndirection(TurningDirection turning_direction)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 0)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(0).first.SetTurningDirection(turning_direction);
			Serial.println("SetFirstwatchturndirection() SUCCESS");
		}
	}
}

void WatchWinder::SetSecondwatchname(String name)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 1)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(1).first.SetName(name);
			Serial.println("SetSecondwatchname() SUCCESS");
		}
	}
}

void WatchWinder::SetSecondwatchturnsperday(int revolutions_per_day)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 1)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(1).first.SetRevolutionsPerDay(revolutions_per_day);
			Serial.println("SetSecondwatchturnsperday() SUCCESS");
		}
	}
}

void WatchWinder::SetSecondwatchturndirection(TurningDirection turning_direction)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 1)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(1).first.SetTurningDirection(turning_direction);
			Serial.println("SetSecondwatchturndirection() SUCCESS");
		}
	}
}

void WatchWinder::SetThirdwatchname(String name)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 2)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(2).first.SetName(name);
			Serial.println("SetThirdwatchname() SUCCESS");
		}
	}
}

void WatchWinder::SetThirdwatchturnsperday(int revolutions_per_day)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 2)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(2).first.SetRevolutionsPerDay(revolutions_per_day);
			Serial.println("SetThirdwatchturnsperday() SUCCESS");
		}
	}
}

void WatchWinder::SetThirdwatchturndirection(TurningDirection turning_direction)
{
	if (watch_movement_suppliers_.size() > 0)
	{
		if (watch_movement_suppliers_.at(0).GetAllRequirements().size() > 2)
		{
			watch_movement_suppliers_.at(0).GetAllRequirements().at(2).first.SetTurningDirection(turning_direction);
			Serial.println("SetThirdwatchturndirection() SUCCESS");
		}
	}
}

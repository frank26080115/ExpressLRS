function am32_init()
{
    getEleById("btn_readbinfile").addEventListener("change", readBinFile, false);
    getEleById("tbl_checkboxes").innerHTML      = make_all_checkboxes(plain_checkboxes);
    getEleById("tbl_sliders").innerHTML         = make_all_sliders(plain_sliders);
    getEleById("tbl_checkboxes_219").innerHTML  = make_all_checkboxes(more_checkboxes);
    getEleById("tbl_sliders_219").innerHTML     = make_all_sliders(more_sliders) + make_brake_on_zero_throttle();
    getEleById("tbl_extracheckboxes").innerHTML = make_all_checkboxes(extra_checkboxes);
    getEleById("tbl_extrasliders").innerHTML    = make_all_sliders(extra_sliders);
    getEleById("btn_fwupdate").addEventListener("change", fwupdate, false);

    getEleById("chk_drivebyrpm").addEventListener('change', function() {
        enabledisable_elements_with("_speedctrl", this.checked);
    });
    getEleById("chk_variablepwm").addEventListener('change', function() {
        enabledisable_elements_with("_pwmfrequency", !this.checked);
    });
    getEleById("chk_brushedmode").addEventListener('change', function() {
        enabledisable_elements_with("chk_dualbrushedmode", this.checked);
        if (this.checked == false) {
            getEleById("chk_dualbrushedmode").checked = false;
            getEleById("chk_dualbrushedmode").dispatchEvent(new Event('change'));
        }
    });
    getEleById("chk_lowvoltagecutoff").addEventListener('change', function() {
        enabledisable_elements_with("txt_lowvoltagecutoff", this.checked);
        enabledisable_elements_with("sld_lowvoltagecutoff", this.checked);
    });
    getEleById("chk_dualbrushedmode").addEventListener('change', function() {
        if (this.checked) {
            getEleById("chk_brushedmode").checked = true;
            let inputmode = getEleById("drop_rcinput").value;
            if (inputmode != "x_3") {
                getEleById("drop_rcinput").value = "x_3";
            }
            drop_rcinput_onchange();
        }
        else {
            drop_rcinput_onchange();
        }
    });

    fn_saveByteArray = (function () {
        var a = document.createElement("a");
        document.body.appendChild(a);
        a.style = "display: none";
        return function (data, name) {
            var blob = new Blob([data], {type: "octet/stream"});
            var url = window.URL.createObjectURL(blob);
            a.href = url;
            a.download = name;
            a.click();
            window.URL.revokeObjectURL(url);
            console.log("Local binary file saved");
        };
    }());

    am32_init2();
}

async function am32_init2()
{
    await fill_rcinputs();
    getEleById("div_loading").style.display = "none";
    getEleById("div_maincontent").style.display = "block";
    getEleById("div_testesc").style.display = "block";
    getEleById("div_escconnect").style.display = "block";
    if (!isRunningLocally())
    {
        //getEleById("btn_serwrite").style.display = "none";
        getEleById("btn_serwrite").disabled = true;
        getEleById("div_maincontent").style.display = "none";
        getEleById("div_testesc").style.display = "none";
        getEleById("fld_crsfchannels").style.display = "none";
        getEleById("fld_crsf2channels").style.display = "none";
        getEleById("div_experimentalextras").style.display = "none";
    }
    getEleById("chk_drivebyrpm").dispatchEvent(new Event('change'));
    getEleById("chk_variablepwm").dispatchEvent(new Event('change'));
    getEleById("chk_lowvoltagecutoff").dispatchEvent(new Event('change'));
    getEleById("chk_brushedmode").dispatchEvent(new Event('change'));
    getEleById("chk_dualbrushedmode").dispatchEvent(new Event('change'));
    drop_rcinput_onchange();
}

async function fill_rcinputs()
{
    let data;
    if (!isRunningLocally()) {
        let formData = new FormData();
        formData.append("action", srvaction_pin_list);
        let response = await fetch("/am32io", { method: 'POST', body: formData});
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        data = await response.text();
    }
    else {
        data = "PWM 1 2,DSHOT 3 4,DSHOT3D 5 6,SERRX 7, SERTX 8";
    }
    let pin_strs = data.split(',');
    let select = getEleById('drop_selpin');

    let prev_idx = null;
    if (select.options.length > 0) {
        prev_idx = select.selectedIndex; // remember so we can reselect
        select.options.length = 0; // clears the list
    }

    for (let pin_str of pin_strs) {
        try {
            let opt = document.createElement('option');
            let pin_parts = pin_str.split(' ');
            let pin_num;
            if (pin_parts[0] == "PWM" || pin_parts[0] == "DSHOT" || pin_parts[0] == "DSHOT3D") {
                pin_num = parseInt(pin_parts[2]);
                let ch_num = parseInt(pin_parts[1]);
                let ch_num_print = ch_num + 1;
                opt.value = pin_num;
                opt.innerHTML = `${pin_parts[0]} CH-${ch_num_print} PIN-${pin_num}`;
            }
            else if (pin_parts[0].startsWith("SERTX")) {
                pin_num = parseInt(pin_parts[1]);
                opt.value = pin_num;
                opt.innerHTML = `SERIAL-TX PIN-${pin_num}`;
            }
            else if (pin_parts[0].startsWith("SERRX")) {
                pin_num = parseInt(pin_parts[1]);
                opt.value = pin_num;
                opt.innerHTML = `SERIAL-RX PIN-${pin_num}`;
            }
            if (opt.innerHTML.trim().length > 0) {
                pin_been_tried[pin_num] = false;
                select.appendChild(opt);
            }
        }
        catch (e) {
        }
    }

    if (prev_idx != null) {
        select.selectedIndex = prev_idx;
    }
}

function getEleById(i) {
    return document.getElementById(i);
}

let mcu_data = [
    {
        "name": "Generic 32K",
        "signature": [0x1F, 0x06],
        "eeprom_start": 0x7C00,
        "flash_start": 0x1000,
        "addr_multi": 1
    },
    {
        "name": "Generic 64K",
        "signature": [0x35, 0x06],
        "eeprom_start": 0xF800,
        "flash_start": 0x1000,
        "addr_multi": 1
    },
    {
        "name": "Generic 128K",
        "signature": [0x2B,0x06],
        "eeprom_start": 0x1F800,
        "flash_start": 0x1000,
        "addr_multi": 4
    }
];

let pin_been_tried = {};

const srvaction_pin_list = 0;
const srvaction_pin_low = 1;
const srvaction_pin_high = 2;
const srvaction_ser_init = 3;
const srvaction_ser_read = 4;
const srvaction_ser_write = 5;
const srvaction_test_start = 6;
const srvaction_test_pulse = 7;
const srvaction_change_dshot_mode = 8;

let current_chip = null;

let flash_write_chunk = 128;
let eeprom_useful_length = 0x30;
let eeprom_total_length = 0xB0; // 176

let plain_checkboxes = [
    // Name                  , def  , byte, true-value
    ["Reverse Rotation",       false, 17, ],
    ["Complementary PWM",      true , 20, ],
    ["Variable PWM",           true , 21, ],
    ["Bi-Directional",         false, 18, ],
    ["Stuck Rotor Protection", false, 22, ],
    ["Brake On Stop",          false, 28, ],
    ["Stall Protection",       false, 29, ],
    ["Sinusoidal Startup",     false, 19, ],
    ["Telemetry 30ms",         false, 31, ],
    ["Use Hall Sensors",       false, 39, ],
    ["Auto Advance",           false, 47, ],
    ["Low Voltage Cutoff",     false, 36, ],
    ["Double Tap Reverse",     false, 38, ],
];

let more_checkboxes = [
    ["Disable Stick Calibration", false, 7, ],
];

let extra_checkboxes = [
    ["Automatic Timing",       false, 53, ],
    ["Drive by RPM",           false, 54, ],
    ["Brushed Mode",           false, 64, 85],
    ["Dual Brushed Mode",      false, 69, 85],
];

let plain_sliders = [
    // Name                  ,  def ,  min ,  max ,   step, offset, byte, readonly
    ["Timing Advance"        ,     2,     0,     3,      1,      0,   23, false, ],
    ["Motor KV"              ,  2000,    20, 10220,     40,     20,   26, false, ],
    ["Motor Poles"           ,    14,     0,   255,      1,      0,   27, false, ],
    ["Startup Power"         ,   100,    50,   150,      1,      0,   25, false, ],
    ["PWM Frequency"         ,    24,     8,    48,      1,      0,   24, false, ],
    ["Beep Volume"           ,     7,     0,    10,      1,      0,   30, false, ],
    ["Stopped Brake Level"   ,    10,     0,    10,      1,      0,   41, false, ],
    ["Running Brake Level"   ,     9,     0,     9,      1,      0,   42, false, ],
    ["Sine Startup Range"    ,     5,     5,    25,      1,      0,   40, false, ],
    ["Sine Mode Power"       ,     5,     0,    10,      1,      0,   45, false, ],
    ["Servo Low Thresh"      ,  1000,   750,  1500,      2,    750,   32, false, ],
    ["Servo High Thresh"     ,  2000,  1750,  2260,      2,   1750,   33, false, ],
    ["Servo Neutral"         ,  1500,  1374,  1629,      1,   1374,   34, false, ],
    ["Servo Dead Band"       ,     0,     0,   255,      1,      0,   35, false, ],
    ["Low Voltage Cutoff"    ,   330,   250,   505,      1,    250,   37, false, ],
    ["Temperature Limit C"   ,   255,     0,   255,      1,      0,   43, false, ],
    ["Current Limit Amps"    ,     0,     0,   202,      2,      0,   44, false, ],
];

let more_sliders = [
    ["Max Ramp"              ,   160,     0,   255,      1,      0,    5, false, ],
    ["Minimum Duty Cycle"    ,     2,     2,   512,      2,      0,    6, false, ],
    ["Abs. Voltage Cutoff"   ,     0,     0,  1275,      5,      0,    8, false, ],
    ["Current K_P"           ,   100,     0,   255,      1,      0,    9, false, ],
    ["Current K_I"           ,     0,     0,   255,      1,      0,   10, false, ],
    ["Current K_D"           ,   100,     0,   255,      1,      0,   11, false, ],
    ["Active Brake Power"    ,     0,     0,   255,      1,      0,   12, false, ],
];

let extra_sliders = [
//    ["Speed Ctrl Min Input"  ,    47,    47,  5147,     20,     47,   55, false, ],
//    ["Speed Ctrl Max Input"  ,  5147,    47,  5147,     20,     47,   56, false, ],
    ["Speed Ctrl Min RPM"    ,     0,     0, 25500,    100,      0,   57, false, ],
    ["Speed Ctrl Max RPM"    , 51000,     0, 51000,    200,      0,   58, false, ],
    ["Speed Ctrl PID Kp"     ,    10,     0,   255,      1,      0,   59, false, ],
    ["Speed Ctrl PID Ki"     ,     0,     0,   255,      1,      0,   60, false, ],
    ["Speed Ctrl PID Kd"     ,     0,     0,   255,      1,      0,   61, false, ],
    ["ROTC Divider"          ,     0,     0,   255,      1,      0,   62, false, ],
    ["Minimum Duty Cycle"    ,     0,     0,  5100,     20,      0,   63, false, ],
    ["RGB LED Red"           ,     0,     0,   255,      1,      0,   66, false, ],
    ["RGB LED Green"         ,   255,     0,   255,      1,      0,   67, false, ],
    ["RGB LED Blue"          ,     0,     0,   255,      1,      0,   68, false, ],
];

let all_sliders = [];
all_sliders = all_sliders.concat(plain_sliders, more_sliders, extra_sliders);
let all_checkboxes = [];
all_checkboxes = all_checkboxes.concat(plain_checkboxes, more_checkboxes, extra_checkboxes);

let fn_saveByteArray; // this will be a function

let ui_locked = false;

function isRunningLocally() {
    return window.location.hostname === "localhost" || window.location.hostname === "127.0.0.1" || window.location.protocol === "file:";
}

function fakeit(el, v) {
    // call this to make it seem like something is connected
    current_chip = {};
    current_chip["eeprom_0"] = 1;
    current_chip["eeprom_layout"] = el;
    current_chip["bootloader_version"] = 8;
    current_chip["fw_version_major"] = v;
    current_chip["fw_version_minor"] = 1;
    current_chip["mcu"] = mcu_data[0];
    testesc_idleval = 1500;
    fill_version_box();
    offer_experimental();
}

function text_to_id(t)
{
    return t.toLowerCase().replaceAll(" ", "").replaceAll("-", "").replaceAll(".", "");
}

function make_checkbox(x)
{
    let t;
    t = "<div style=\"display: table-cell; text-align: right;\"><label for=\"chk_" + text_to_id(x[0]) + "\">" + x[0] + "</label></div>\r\n";
    t += "<div style=\"display: table-cell; text-align: left\"><input id=\"chk_" + text_to_id(x[0]) + "\" type=\"checkbox\" onchange=\"chkbox_onchange('" + text_to_id(x[0]) + "')\"";
    if (x[1])
    {
        t += " checked=\"checked\"";
    }
    t += " /></div>\r\n";
    return t;
}

function make_slider(x, i)
{
    let t;
    t = "<div style=\"display: table-cell; text-align: right; padding-right:5pt;\"><label for=\"txt_" + text_to_id(x[0]) + "\">" + x[0] + "</label></div>\r\n";
    t += "<div style=\"display: table-cell; text-align: left; padding-right: 10pt;\"><input id=\"txt_" + text_to_id(x[0]) + "\" type=\"number\" style=\"width:100%; text-align: right;\"";
    t += "min=\"" + x[2] + "\" max=\"" + x[3] + "\" value=\"" + x[1] + "\" step=\"" + x[4] + "\" ";
    t += "onchange=\"txt_onchange('" + text_to_id(x[0]) + "')\"";
    if (x[7]) {
        t += " readonly disabled";
    }
    t += " /></div>\r\n";
    t += "<div style=\"display: table-cell;\"><input id=\"sld_" + text_to_id(x[0]) + "\" type=\"range\" ";
    t += "min=\"" + x[2] + "\" max=\"" + x[3] + "\" value=\"" + x[1] + "\" step=\"" + x[4] + "\" ";
    t += "onchange=\"sld_onchange('" + text_to_id(x[0]) + "')\"";
    t += " oninput=\"sld_onchange('" + text_to_id(x[0]) + "')\"";
    if (x[7]) {
        t += " readonly disabled";
    }
    t += " /></div>\r\n";
    t += "<div style=\"display: table-cell; padding-left:5pt;\">def = " + x[1] + "</div>\r\n";
    return t;
}

function make_all_checkboxes(chk_arr)
{
    let t = "";
    for (let i = 0; i < chk_arr.length; i++)
    {
        t += "<div style=\"display: table-row;\">\r\n";
        t += make_checkbox(chk_arr[i]);
        t += "</div>\r\n";
    }
    return t;
}

function make_all_sliders(sld_arr)
{
    let t = "";
    for (let i = 0; i < sld_arr.length; i++)
    {
        t += "<div style=\"display: table-row;\">\r\n";
        t += make_slider(sld_arr[i], i);
        t += "</div>\r\n";
    }
    return t;
}

function make_brake_on_zero_throttle()
{
    let t = "<div id=\"row_brakeonzerothrottle\" style=\"display: none;\">\r\n";
    t += "<div style=\"display: table-cell; text-align: right; padding-right:5pt;\"><label for=\"drop_brakeonzerothrottle\">Brake On Zero Throttle</label></div>\r\n";
    t += "<div style=\"display: table-cell; text-align: left; padding-right:10pt;\"><select id=\"drop_brakeonzerothrottle\">\r\n";
    t += "<option value=\"0\">Normal</option>\r\n";
    t += "<option value=\"1\">Coast</option>\r\n";
    t += "<option value=\"2\">Brake</option>\r\n";
    for (let seconds = 1; seconds <= 7; seconds++) {
        t += `<option value="${seconds + 2}">Brake after ${seconds} second${seconds == 1 ? "" : "s"}</option>\r\n`;
    }
    t += "</select></div>\r\n";
    t += "</div>\r\n";
    return t;
}

function update_brake_on_zero_throttle_visibility()
{
    let row = getEleById("row_brakeonzerothrottle");
    if (row == null) {
        return;
    }
    let is_supported = current_chip != null && current_chip["eeprom_layout"] >= 4;
    row.style.display = is_supported ? "table-row" : "none";
}

function txt_onchange(i)
{
    if (ui_locked) {
        return;
    }
    ui_locked = true;
    for (let sld of all_sliders)
    {
        if (i == text_to_id(sld[0]))
        {
            let v = getEleById("txt_" + text_to_id(sld[0])).value;
            v = Math.max(sld[2], Math.min(sld[3], v));
            getEleById("sld_" + text_to_id(sld[0])).value = v;
        }
    }
    ui_locked = false;
}

function sld_onchange(i)
{
    if (ui_locked) {
        return;
    }
    ui_locked = true;
    for (let sld of all_sliders)
    {
        if (i == text_to_id(sld[0]))
        {
            let v = getEleById("sld_" + text_to_id(sld[0])).value;
            v = Math.max(sld[2], Math.min(sld[3], v));
            getEleById("txt_" + text_to_id(sld[0])).value = v;
        }
    }
    ui_locked = false;
}

function chkbox_onchange(i)
{
    //if (getEleById("chk_bidirectional").checked) {
    //    getEleById("chk_complementarypwm").checked = true;
    //}

    if (i == "bidirectional") {
        const dropdown = getEleById("drop_selpin");
        if (dropdown) {
            const value = dropdown.value;
            let pin_text = null;

            const options = dropdown.options;
            for (let i = 0; i < options.length; i++) {
                if (options[i].value === value) {
                    pin_text = options[i].text;
                }
            }

            if (pin_text != null) {
                if (pin_text.startsWith("DSHOT3D")) {
                    if (getEleById("chk_bidirectional").checked == false) {
                        cuteAlert({
                            type: 'question',
                            title: 'Change DShot Mode?',
                            message: `Disabling bi-directional mode on AM32 requires the DSHOT mode to be non-3D, change the DSHOT mode to non-3D?`,
                            confirmText: 'Yes',
                            cancelText: 'No'
                        }).then((e)=>{
                            serport_ajax("send change dshot mode", srvaction_change_dshot_mode, serport_lastpin, null, 0).then(result=>{
                                fill_rcinputs();
                            });
                        });
                    }
                }
                else if (pin_text.startsWith("DSHOT")) {
                    if (getEleById("chk_bidirectional").checked) {
                        cuteAlert({
                            type: 'question',
                            title: 'Change DShot Mode?',
                            message: `Bi-directional mode on AM32 requires the DSHOT mode to be DSHOT-3D, change the DSHOT mode?`,
                            confirmText: 'Yes 3D',
                            cancelText: 'No'
                        }).then((e)=>{
                            serport_ajax("send change dshot mode", srvaction_change_dshot_mode, serport_lastpin, null, 1).then(result=>{
                                fill_rcinputs();
                            });
                        });
                    }
                }
            }
        }
    }
}

function drop_rcinput_onchange()
{
    let v = getEleById("drop_rcinput").value;
    let can = isRunningLocally();
    if (can == false)
    {
        if (current_chip != null)
        {
            // only the latest experimental versions can see the extra inputs for CRSF channel
            if (current_chip["fw_version_major"] >= 3 && current_chip["eeprom_layout"] >= 5) {
                can = true;
            }
            if (getEleById("chk_experimentalupgrade").checked) {
                can = true;
            }
        }
    }

    if (can && v == "x_3") { // serial input enforced
        getEleById("fld_crsfchannels").style.display = "block";
        if (getEleById("chk_dualbrushedmode").checked) {
            getEleById("fld_crsf2channels").style.display = "block";
        }
        else {
            getEleById("fld_crsf2channels").style.display = "none";
        }
    }
    else { // serial input not enforced
        getEleById("fld_crsfchannels").style.display = "none";
        getEleById("fld_crsf2channels").style.display = "none";
    }
}

function enabledisable_elements_with(name, sts)
{
    // this function is used to quickly enable or disable any elements matching *_speedcrtl*
    let inputs = document.getElementsByTagName('input');
    for (var i = 0; i < inputs.length; i++) {
        // Check if the ID contains the search string
        if (inputs[i].id.includes(name)) {
            // Disable the input element
            inputs[i].disabled = !sts;
        }
    }
}

function readBinFile(e)
{
    let file = e.target.files[0];
    if (!file) {
        return;
    }
    let reader = new FileReader();
    reader.onload = function(e) {
        let barr = new Uint8Array(e.target.result);
        readBin(barr, true);
        console.log("Local binary file loaded");
        getEleById("btn_readbinfile").value = "";
    };
    reader.readAsArrayBuffer(file);
}

function readBin(barr, isFile)
{
    let dbg_txt = "";
    let skip_form_fill = false;

    try {
        if (barr[0] == 0 || barr[0] == 0xFF) {
            dbg_txt += "warning: EEPROM appears empty\r\n";
            if (isFile == false) {
                skip_form_fill = true;
            }
        }
        if (barr[1] == 0 || barr[1] == 0xFF) {
            dbg_txt += "warning: EEPROM has no layout indicator\r\n";
        }
        if (barr[2] == 0 || barr[2] == 0xFF) {
            dbg_txt += "warning: bootloader version invalid\r\n";
        }

        if (current_chip == null) {
            current_chip = {};
        }

        if (isFile == false)
        {
            current_chip["eeprom_0"] = barr[0];
            current_chip["eeprom_layout"] = barr[1];
            current_chip["bootloader_version"] = barr[2];
            current_chip["fw_version_major"] = barr[3];
            current_chip["fw_version_minor"] = barr[4];
        }

        if (skip_form_fill == false)
        {
            let allow_extras;
            //allow_extras = getEleById("chk_experimentalupgrade").checked;
            allow_extras = current_chip["eeprom_layout"] >= 5 && current_chip["fw_version_major"] >= 3;

            let is_219;
            is_219 = current_chip["eeprom_layout"] >= 3 && current_chip["fw_version_major"] == 2 && current_chip["fw_version_minor"] >= 19;
            current_chip["is_219_or_later"] = is_219;

            let extras_bidx = extra_checkboxes[0][2];
            for (let i = 0; i < all_checkboxes.length; i++)
            {
                let c = all_checkboxes[i];
                let bidx = c[2];
                if (bidx >= extras_bidx && allow_extras == false) {
                    continue;
                }
                let ele = getEleById("chk_" + text_to_id(c[0]));
                ele.checked = barr[bidx] != 0;
            }

            for (let i = 0; i < all_sliders.length; i++)
            {
                let sld = all_sliders[i];
                let bidx = sld[6];
                if (bidx >= extras_bidx && allow_extras == false) {
                    continue;
                }
                let eles = getEleById("sld_" + text_to_id(sld[0]));
                let elet = getEleById("txt_" + text_to_id(sld[0]));
                let val = barr[bidx];
                val *= sld[4];
                val += sld[5];
                if (val < sld[2] || val > sld[3])
                {
                    dbg_txt += "\"" + sld[0] + "\" value " + val + " is out of range\r\n";
                    val = Math.min(sld[3], Math.max(sld[2], val));
                }
                elet.value = val;
                eles.value = val;
                txt_onchange(text_to_id(sld[0]));
            }

            update_brake_on_zero_throttle_visibility();
            if (current_chip["eeprom_layout"] >= 4) {
                let brake_on_zero_throttle = barr[13];
                if (brake_on_zero_throttle > 9) {
                    dbg_txt += "Brake On Zero Throttle value " + brake_on_zero_throttle + " is out of range\r\n";
                    brake_on_zero_throttle = 0;
                }
                getEleById("drop_brakeonzerothrottle").value = brake_on_zero_throttle;
            }

            let drop_rcinput = getEleById("drop_rcinput");
            if (barr[46] >= 0 && barr[46] < 10) {
                drop_rcinput.value = "x_" + barr[46].toString();
            }
            else {
                dbg_txt += "RC input value " + barr[46] + " is out of range\r\n";
                drop_rcinput.value = "x_0";
            }

            let txt_devicename = getEleById("txt_devicename");
            let dev_name = "";
            
            if (!is_219) {
                for (let i = 0; i < 12; i++)
                {
                    let x = barr[5 + i];
                    if (x == 0 || x == 0xFF) {
                        break;
                    }
                    dev_name += String.fromCharCode(x);
                }
                txt_devicename.disabled = false;
                getEleById("tbl_checkboxes_219").style.display = "none";
                getEleById("tbl_sliders_219").style.display = "none";
            }
            else {
                // device name is not a part of EEPROM in v2.19 or later
                txt_devicename.disabled = true;
                getEleById("tbl_checkboxes_219").style.display = "table-row-group";
                getEleById("tbl_sliders_219").style.display = "table-row-group";
            }

            txt_devicename.value = dev_name;

            let txt_crsfchannel = getEleById("txt_crsfchannel");
            txt_crsfchannel.value = barr[65] + 1;
            let txt_crsf2channel = getEleById("txt_crsf2channel");
            txt_crsf2channel.value = barr[70] + 1;
        }

    }
    catch (e) {
        dbg_txt += "ERROR while reading binary: " + e.toString();
    }

    if (dbg_txt.length > 0) {
        console.log(dbg_txt);
    }
}

function generateBin()
{
    let dbg_txt = "";
    let buffer = new ArrayBuffer(eeprom_total_length);
    let buffer8 = new Uint8Array(buffer);
    for (let i = 0; i < buffer8.length; i++)
    {
        buffer8[i] = 0xFF;
    }
    buffer8[0] = 1; // indicate filled

    if (getEleById("chk_experimentalupgrade").checked) {
        if (current_chip == null) {
            current_chip = {};
        }
        if (current_chip.hasOwnProperty("eeprom_layout")) {
            if (current_chip["eeprom_layout"] < 5) {
                current_chip["eeprom_layout"] = 5;
            }
        }
        else {
            current_chip["eeprom_layout"] = 5;
        }
        if (current_chip.hasOwnProperty("fw_version_major")) {
            if (current_chip["fw_version_major"] < 3) {
                current_chip["fw_version_major"] = 3;
                current_chip["fw_version_minor"] = 0;
            }
            if (current_chip.hasOwnProperty("fw_version_minor")) {
                current_chip["fw_version_minor"] = 0;
            }
        }
        else {
            current_chip["fw_version_major"] = 3;
            current_chip["fw_version_minor"] = 0;
        }
        if (!current_chip.hasOwnProperty("bootloader_version")) {
            current_chip["bootloader_version"] = 0;
        }
    }

    if (current_chip != null) {
        buffer8[1] = current_chip["eeprom_layout"];
        buffer8[2] = current_chip["bootloader_version"];
        buffer8[3] = current_chip["fw_version_major"];
        buffer8[4] = current_chip["fw_version_minor"];
        fill_version_box();
    }
    else {
        buffer8[1] = 4;
        buffer8[2] = 0;
        buffer8[3] = 1;
        buffer8[4] = 0;
    }

    for (let i = 0; i < all_checkboxes.length; i++)
    {
        let c = all_checkboxes[i];
        let ele = getEleById("chk_" + text_to_id(c[0]));
        buffer8[c[2]] = ele.checked ? (c.length <= 3 ? 1 : c[3]) : 0; // apply truth-value if available, otherwise, apply 1
    }
    for (let i = 0; i < all_sliders.length; i++)
    {
        let sld = all_sliders[i];
        let ele = getEleById("txt_" + text_to_id(sld[0]));
        let val = ele.value;
        val -= sld[5];
        val /= sld[4];
        if (val < 0 || val > 255) {
            dbg_txt += "\"" + sld[0] + "\" byte value " + val + " is overflowing\r\n";
            val = Math.min(0, Math.max(255, val));
        }
        buffer8[sld[6]] = Math.round(val);
    }

    let drop_rcinput = getEleById("drop_rcinput");
    buffer8[46] = Math.round(parseInt(drop_rcinput.value.substring(2)));

    if (current_chip["eeprom_layout"] >= 4) {
        buffer8[13] = Math.round(parseInt(getEleById("drop_brakeonzerothrottle").value));
    }

    let is_219;
    is_219 = current_chip["eeprom_layout"] >= 3 && current_chip["fw_version_major"] == 2 && current_chip["fw_version_minor"] >= 19;

    let txt_devicename = getEleById("txt_devicename");
    let i;
    if (!is_219) {
        for (i = 0; i < 12 && i < txt_devicename.value.length; i++)
        {
            buffer8[5 + i] = Math.round(txt_devicename.value.charCodeAt(i)) & 0xFF;
        }
        for (; i < 12; i++)
        {
            buffer8[5 + i] = 0;
        }
        getEleById("tbl_checkboxes_219").style.display = "none";
        getEleById("tbl_sliders_219").style.display = "none";
    }
    else {
        getEleById("tbl_checkboxes_219").style.display = "table-row-group";
        getEleById("tbl_sliders_219").style.display = "table-row-group";
    }

    let txt_crsfchannel = getEleById("txt_crsfchannel");
    buffer8[65] = txt_crsfchannel.value - 1;
    let txt_crsf2channel = getEleById("txt_crsf2channel");
    buffer8[70] = txt_crsf2channel.value - 1;

    if (dbg_txt.length > 0) {
        console.log(dbg_txt);
    }

    return buffer8;
}

function getBinFileName(fname, default_name)
{
    if (fname.length > 0) {
        if (filename_isValid(fname)) {
            if (fname.toLowerCase().endsWith(".bin") == false) {
                fname += ".bin";
            }
            while (fname.includes("..")) {
                fname = fname.replaceAll("..", ".");
            }
        }
        else {
            console.log("warning: invalid save file name, using default name instead");
            fname = "";
        }
    }
    if (fname.length <= 0) {
        fname = default_name;
    }
    return fname;
}

function saveBinFile()
{
    let fname = getEleById("txt_savefname").value;
    fn_saveByteArray(generateBin(), getBinFileName(fname, "am32-eeprom.bin"));
}

function fill_version_box()
{
    if (current_chip != null)
    {
        getEleById("span_firmwareversion").innerHTML = `V${current_chip["fw_version_major"]}.${current_chip["fw_version_minor"]} EL${current_chip["eeprom_layout"]} BL${current_chip["bootloader_version"]}`;
    }
}

function offer_experimental()
{
    if (current_chip != null)
    {
        if (current_chip["fw_version_major"] >= 3 && current_chip["eeprom_layout"] >= 5) {
            getEleById("div_experimentalextras").style.display = "block";
            getEleById("span_experimentalupgrade").style.display = "none";
            getEleById("chk_experimentalupgrade").checked = true;
        }
        else {
            getEleById("div_experimentalextras").style.display = "none";
            getEleById("span_experimentalupgrade").style.display = "inline";
            getEleById("chk_experimentalupgrade").checked = false;
        }
    }
    else
    {
        getEleById("div_experimentalextras").style.display = "none";
        getEleById("span_experimentalupgrade").style.display = "inline";
        getEleById("chk_experimentalupgrade").checked = false;
    }
}

function chk_experimentalupgrade_onchange()
{
    let c = getEleById("chk_experimentalupgrade").checked;
    if (c) {
        getEleById("div_experimentalextras").style.display = "block";
    }
    else {
        getEleById("div_experimentalextras").style.display = "none";
    }
}

let filename_isValid=(function(){
  let rg1=/^[^\\/:\*\?"<>\|]+$/; // forbidden characters \ / : * ? " < > |
  let rg2=/^\./; // cannot start with dot (.)
  let rg3=/^(nul|prn|con|lpt[0-9]|com[0-9])(\.|$)/i; // forbidden file names
  return function filename_isValid(fname){
    return rg1.test(fname)&&!rg2.test(fname)&&!rg3.test(fname);
  }
})();

function toHexString(x)
{
    let hex = "";
    if (typeof x === "number") {
        x = [x];
    }
    for (let y of x)
    {
        let hex8 = y.toString(16);
        while ((hex8.length % 2) != 0) {
            hex8 = "0" + hex8;
        }
        hex += hex8;
    }
    return hex.toUpperCase();
}

function fromHexString(x)
{
    let byteList = [];
    for (let i = 0; i < x.length; i += 2) {
        let byte = parseInt(x.substr(i, 2), 16);
        byteList.push(byte);
    }
    return byteList;
}

function objectToFormData(obj) {
    let formData = new FormData();
    for (let key in obj) {
        formData.append(key, obj[key]);
    }
    return formData;
}

function uint8ArrayMerge(x, y)
{
    if (x == null)
    {
        x = new Uint8Array(y.length);
        for (let i = 0; i < y.length; i++) {
            x[i] = y[i];
        }
    }
    else
    {
        let mergedArray = new Uint8Array(x.length + y.length);
        mergedArray.set(x);
        for (let i = 0, j = x.length; i < y.length && j < mergedArray.length; i++, j++) {
            mergedArray[j] = y[i];
        }
        x = mergedArray; 
    }
    return x;
}

function serport_genSetAddressCmd(adr)
{
    let x = [0xFF, 0x00, 0x00, 0x00];
    x[2] = (adr & 0xFF00) >> 8;
    x[3] = (adr & 0x00FF) >> 0;
    let crc = serport_genCrc(x);
    x.push((crc & 0x00FF) >> 0);
    x.push((crc & 0xFF00) >> 8);
    return x;
}

function serport_genSetBufferCmd(x256, len)
{
    let x = [0xFE, 0x00, x256, len];
    let crc = serport_genCrc(x);
    x.push((crc & 0x00FF) >> 0);
    x.push((crc & 0xFF00) >> 8);
    return x;
}

function serport_genReadCmd(rdLen)
{
    let x = [0x03, rdLen];
    let crc = serport_genCrc(x);
    x.push((crc & 0x00FF) >> 0);
    x.push((crc & 0xFF00) >> 8);
    return x;
}

function serport_genInitQuery()
{
    return [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0D, 0x42, 0x4C, 0x48, 0x65, 0x6C, 0x69, 0xF4, 0x7D];
}

function serport_genPayload(bin, start, len)
{
    //let x = new Uint8Array(len + 2);
    let x = [];
    for (let i = 0; i < len; i++)
    {
        if (i < bin.length) {
            //x[i] = bin[start + i];
            x.push(bin[start + i]);
        }
        else {
            //x[i] = 0;
            x.push(0);
        }
    }
    let crc = serport_genCrc(x);
    x[len    ] = ((crc & 0x00FF) >> 0);
    x[len + 1] = ((crc & 0xFF00) >> 8);
    return x;
}

function serport_genFlashCmd()
{
    let x = [0x01, 0x01];
    let crc = serport_genCrc(x);
    x.push((crc & 0x00FF) >> 0);
    x.push((crc & 0xFF00) >> 8);
    return x;
}

function serport_genRunCmd()
{
    let x = [0x00, 0x00, 0x00, 0x00];
    let crc = serport_genCrc(x);
    x.push((crc & 0x00FF) >> 0);
    x.push((crc & 0xFF00) >> 8);
    return x;
}

function serport_genCrc(barr)
{
    let crc16 = new Uint16Array(1);
    let xb = new Uint8Array(1);
    crc16[0] = 0;
    for (let i = 0; i < barr.length; i++)
    {
        xb[0] = barr[i] & 0xFF;
        for (let j = 0; j < 8; j++)
        {
            if (((xb[0] & 0x01) ^ (crc16[0] & 0x0001)) != 0) {
                crc16[0] = (crc16[0] >> 1) & 0xFFFF;
                crc16[0] = (crc16[0] ^ 0xA001) & 0xFFFF;
            }
            else {
                crc16[0] = (crc16[0] >> 1) & 0xFFFF;
            }
            xb[0] = (xb[0] >> 1) & 0xFF;
        }
    }
    return crc16[0] & 0xFFFF;
}

function serport_verifyCrc(barr)
{
    let contents = barr.slice(0, -3);
    let calculated_crc = serport_genCrc(contents);
    let received_crc = barr.slice(-3);
    received_crc = (received_crc[0]) + (received_crc[1] << 8);
    return calculated_crc == received_crc;
}

let serport_lastpin;

async function serport_ajax(msg, action, pinnum, tx_data, delay, tx_data2) {
    let objdata = {"action": action};

    if (pinnum !== undefined) {
        objdata["pin"] = pinnum;
        serport_lastpin = pinnum;
    }
    else {
        objdata["pin"] = serport_lastpin;
    }
    if (tx_data !== undefined) {
        if (tx_data != null) {
            let s = toHexString(tx_data);
            objdata["len1"] = s.length / 2;
            objdata["data1"] = s;
            console.log("tx1: " + s);
        }
    }
    if (delay !== undefined) {
        if (delay != null) {
            objdata["delay"] = delay;
        }
    }
    if (tx_data2 !== undefined) {
        if (tx_data2 != null) {
            let s = toHexString(tx_data2);
            objdata["len2"] = s.length / 2;
            objdata["data2"] = s;
            console.log("tx2: " + s);
        }
    }

    let frmdata = objectToFormData(objdata);
    let response = await fetch("/am32io", { method: 'POST', body: frmdata});
    if (!response.ok) {
        throw new Error(`HTTP error while performing "${msg}"! status: ${response.status}`);
    }
    data = await response.text();
    console.log("ajax resp \"" + msg + "\" from backend: " + data);
    if (action == 4) {
        if (data == "xx") {
            data = [];
        }
        else {
            data = fromHexString(data);
        }
    }
    return data;
}

async function serport_ajax_read(num_bytes, total_time) {
    let buffer = [];
    for (let i = 0; i < total_time; i += 50) {
        let chunk = await serport_ajax("read", srvaction_ser_read);
        buffer = buffer.concat(chunk);
        if (buffer.length >= num_bytes) {
            break;
        }
        if (chunk.length <= 0) {
            await new Promise(resolve => setTimeout(resolve, 50));
        }
    }
    return buffer;
}

async function serport_ajax_readAck(msg) {
    let b = await serport_ajax_read(1, 1000);
    if (b.length == 1) {
        if (b[0] == 0x30 || b[0] == 0x98) {
            return true;
        }
    }
    if (msg !== undefined) {
        throw new Error(`did not get ACK for "${msg}", reply was 0x${toHexString(b[0])}`);
    }
    return false;
}

async function serport_ajax_flashRead(start_addr, read_len, chunk_size, addr_multi, is_fwupdate)
{
    let buffer = [];
    let adr = start_addr;
    while (buffer.length < read_len)
    {
        var data = null;
        for (let retries = 3; retries >= 0; retries--)
        {
            try
            {
                data = await serport_ajax("send set address", srvaction_ser_write, serport_lastpin, serport_genSetAddressCmd(adr / addr_multi));
                await serport_ajax_readAck("set address");
                data = await serport_ajax("send read cmd", srvaction_ser_write, serport_lastpin, serport_genReadCmd(chunk_size));
                await new Promise(resolve => setTimeout(resolve, 50));
                let reply_size = chunk_size + 3;
                data = await serport_ajax_read(reply_size, 1000);
                let timedout = false;
                if (data.length < reply_size)
                {
                    timedout = true;
                }
                if (data.length >= 4) {
                    if (data[data.length - 1] != 0x30) {
                        throw new Error(`did not get ACK during read of address ${adr}, reply was 0x${data[data.length - 1].toString(16)}`);
                    }
                    if (serport_verifyCrc(data) == false) {
                        throw new Error(`CRC failed during read of address ${adr}`);
                    }
                    let actual_data = data.slice(0, -3);
                    buffer = buffer.concat(actual_data);
                }
                if (timedout)
                {
                    return buffer;
                }
                break; // all good, no need to retry
            }
            catch (e)
            {
                console.log("serport_ajax_flashRead internal error: " + e.toString());
                if (retries == 0) {
                    throw e;
                }
            }
        }
        adr += chunk_size;
        let percentage;
        if (is_fwupdate) {
            percentage = 50 + Math.max(0, Math.min(50, Math.round(buffer.length * 50 / read_len)));
        }
        else {
            percentage = Math.max(0, Math.min(100, Math.round(buffer.length * 100 / read_len)));
        }
        getEleById("div_progress").innerHTML = `Verifying... ${percentage}% (${buffer.length} / ${read_len} - 0x${toHexString(adr)})`;
    }
    return buffer;
}

async function serport_ajax_flashWrite(contents, start_addr, write_len, chunk_size, addr_multi, is_fwupdate)
{
    let buffer = [];
    let i = 0;
    let adr = start_addr;
    if (write_len > (chunk_size * 4)) {
        getEleById("div_progress").style.display = "block";
        getEleById("div_progress").innerHTML = `Flash Writing...`;
    }
    while (i < write_len)
    {
        let retries = 3;
        while (retries > 0)
        {
            retries -= 1;
            try
            {
                let data = await serport_ajax("send set address", srvaction_ser_write, serport_lastpin, serport_genSetAddressCmd(adr / addr_multi));
                await serport_ajax_readAck("set address");
                let this_chunk_size = chunk_size;
                if (i + this_chunk_size >= write_len) {
                    this_chunk_size = write_len - i;
                }
                data = await serport_ajax("send buffer", srvaction_ser_write, serport_lastpin, serport_genSetBufferCmd(0, this_chunk_size), 800, serport_genPayload(contents, i, this_chunk_size));
                await serport_ajax_readAck("send buffer");
                data = await serport_ajax("flash cmd", srvaction_ser_write, serport_lastpin, serport_genFlashCmd());
                await serport_ajax_readAck("flash cmd");
                break;
            }
            catch (e) {
                console.log("flash write error: " + e.toString());
                if (retries <= 0) {
                    throw e;
                }
            }
        }

        i += chunk_size;
        adr += chunk_size;
        let percentage;
        if (is_fwupdate) {
            percentage = Math.max(0, Math.min(50, Math.round(i * 50 / write_len)));
        }
        else {
            percentage = Math.max(0, Math.min(100, Math.round(i * 100 / write_len)));
        }
        getEleById("div_progress").innerHTML = `Flash Writing... ${percentage}% (${i} / ${write_len} - 0x${toHexString(adr)})`;
    }
}

async function serport_ajax_forceReboot()
{
    await serport_ajax("force reboot", srvaction_ser_write, serport_lastpin, [0, 0, 0, 0], 100, [0, 0, 0, 0]);
    await serport_ajax("setting pin low", srvaction_pin_low, serport_lastpin);
    await new Promise(resolve => setTimeout(resolve, 2000));
    await serport_ajax("setting pin high", srvaction_pin_high, serport_lastpin);
    await new Promise(resolve => setTimeout(resolve, 2000));
}

function btn_connect_onclick()
{
    btn_connect_onclick_a();
}

async function btn_connect_onclick_a()
{
    try
    {
    let need_alert = false;
    if (getEleById("div_maincontent").style.display != "none") {
        need_alert = true;
    }
    getEleById("div_progress").style.display = "none";
    getEleById("drop_selpin").disabled = true;
    getEleById("btn_connect").disabled = true;
    getEleById("btn_serwrite").disabled = true;
    getEleById("btn_fwupdate").disabled = true;
    getEleById("span_pleasewait").style.display = "inline";
    let data;
    let pinnum = getEleById("drop_selpin").value;
    if (!isRunningLocally()) {
        if (pin_been_tried[pinnum] == false) {
            data = await serport_ajax("setting pin low", srvaction_pin_low, pinnum);
            await new Promise(resolve => setTimeout(resolve, 2000));
            data = await serport_ajax("setting pin high", srvaction_pin_high);
            await new Promise(resolve => setTimeout(resolve, 2000));
        }
        data = await serport_ajax("init serial port", srvaction_ser_init);
        await new Promise(resolve => setTimeout(resolve, 200));

        let mcu = null;

        for (let attempt = 3; attempt > 0; attempt--)
        {
            try
            {
                data = await serport_ajax("send query", srvaction_ser_write, pinnum, serport_genInitQuery());
                let signature_bytes = await serport_ajax_read(9, 1000);
                if (signature_bytes.length < 9) {
                    throw new Error(`signature bytes are too short (or timed-out reading signature), ESC is likely not connected or not responding.`);
                }
                console.log("signature bytes: " + toHexString(signature_bytes));

                mcu = null;
                for (let m of mcu_data)
                {
                    let sig = m["signature"];
                    if (sig[0] == signature_bytes[4] && sig[1] == signature_bytes[5]) {
                        mcu = m;
                        break;
                    }
                }
                if (mcu == null) {
                    throw new Error(`signature bytes do not have a match, the ESC is not responding correctly.`);
                }

                data = await serport_ajax_flashRead(mcu["eeprom_start"], eeprom_total_length, flash_write_chunk, mcu["addr_multi"], false);
                readBin(data, false);
                if (current_chip != null && current_chip.hasOwnProperty("is_219_or_later") && current_chip["is_219_or_later"]) {
                    data = await serport_ajax_flashRead(mcu["eeprom_start"] - 32, 20, flash_write_chunk, mcu["addr_multi"], false);
                    let txt_devicename = getEleById("txt_devicename");
                    let dev_name = "";
                    for (let i = 0; i < 20; i++)
                    {
                        let x = data[i];
                        if (x == 0 || x == 0xFF) {
                            break;
                        }
                        dev_name += String.fromCharCode(x);
                    }
                    txt_devicename.value = dev_name;
                    txt_devicename.disabled = true;
                }
                else {
                    let txt_devicename = getEleById("txt_devicename");
                    txt_devicename.disabled = false;
                }
                break;
            }
            catch (e_inner) {
                console.error(e_inner);
                if (attempt <= 1) {
                    throw e_inner;
                }
                await new Promise(resolve => setTimeout(resolve, 200));
            }
        }

        register_test_params();
        if (current_chip != null) {
            current_chip["mcu"] = mcu;
        }
        pin_been_tried[pinnum] = true;
        fill_version_box();
        offer_experimental();

        getEleById("btn_serwrite").style.display = "inline-block";
        getEleById("btn_serwrite").disabled = false;
        getEleById("btn_fwupdate").disabled = false;
        getEleById("div_maincontent").style.display = "block";
        getEleById("btn_testesc").disabled = false;
        getEleById("div_testesc").style.display = "block";
        getEleById("btn_testesc").value = "Start Test";
    }
    else {
        console.log("pretending to read serial pin " + pinnum);
        await new Promise(resolve => setTimeout(resolve, 2000));
        getEleById("btn_serwrite").style.display = "inline-block";
        getEleById("btn_serwrite").disabled = false;
        getEleById("btn_fwupdate").disabled = false;
        getEleById("div_maincontent").style.display = "block";
        getEleById("btn_testesc").disabled = false;
        getEleById("div_testesc").style.display = "block";
        getEleById("btn_testesc").value = "Start Test";
    }

    if (need_alert) {
        cuteAlert({
            type: 'success',
            title: 'Finished EEPROM read',
            message: 'Finished EEPROM read, this page has been updated'
        });
    }

    }
    catch (e) {
        pin_been_tried[getEleById("drop_selpin").value] = false;
        cuteAlert({
            type: 'error',
            title: 'Error during EEPROM read',
            message: e.toString()
        });
    }

    getEleById("span_pleasewait").style.display = "none";
    getEleById("drop_selpin").disabled = false;
    getEleById("btn_connect").disabled = false;
    testesc_stop();
}

function btn_serwrite_onclick()
{
    btn_serwrite_onclick_a();
}

async function btn_serwrite_onclick_a()
{
    try
    {
    getEleById("div_progress").style.display = "none";
    if (current_chip == null) {
        getEleById("btn_serwrite").disabled = true;
        throw new Error(`not enough data about ESC to proceed`);
    }
    if (!current_chip.hasOwnProperty("mcu")) {
        getEleById("btn_serwrite").disabled = true;
        throw new Error(`not enough data about ESC to proceed`);
    }
    getEleById("drop_selpin").disabled = true;
    getEleById("btn_connect").disabled = true;
    getEleById("btn_serwrite").disabled = true;
    getEleById("btn_fwupdate").disabled = true;
    let data;
    let pinnum = getEleById("drop_selpin").value;
    if (!isRunningLocally()) {
        if (pin_been_tried[pinnum] == false) {
            data = await serport_ajax("setting pin low", srvaction_pin_low, pinnum);
            await new Promise(resolve => setTimeout(resolve, 2000));
            data = await serport_ajax("setting pin high", srvaction_pin_high);
            await new Promise(resolve => setTimeout(resolve, 2000));
            data = await serport_ajax("init serial port", srvaction_ser_init);
            await new Promise(resolve => setTimeout(resolve, 200));
        }

        let eeprom_bin = generateBin();
        await serport_ajax_flashWrite(eeprom_bin, current_chip["mcu"]["eeprom_start"], eeprom_total_length, flash_write_chunk, current_chip["mcu"]["addr_multi"]);
        console.log("eeprom write done, begin verification read");
        data = await serport_ajax_flashRead(current_chip["mcu"]["eeprom_start"], eeprom_total_length, flash_write_chunk, current_chip["mcu"]["addr_multi"], false);
        for (let i = 0; i < eeprom_total_length && i < eeprom_bin.length; i++) {
            if (eeprom_bin[i] != data[i]) {
                throw new Error(`write verification failed at index ${i}`);
            }
        }
        register_test_params();
        pin_been_tried[pinnum] = true;
    }
    else {
        console.log("pretending to send to pin " + pinnum);
        await new Promise(resolve => setTimeout(resolve, 2000));
    }

    cuteAlert({
        type: 'success',
        title: 'Finished EEPROM write',
        message: 'Finished EEPROM write, ESC now has new settings'
    });

    }
    catch (e) {
        cuteAlert({
            type: 'error',
            title: 'Error during EEPROM write',
            message: e.toString()
        });
    }

    getEleById("div_progress").style.display = "none";
    getEleById("btn_serwrite").style.display = "inline-block";
    getEleById("btn_serwrite").disabled = false;
    getEleById("drop_selpin").disabled = false;
    getEleById("btn_connect").disabled = false;
    getEleById("btn_fwupdate").disabled = false;
}

let fwupdate_is_ongoing = false;

async function fwupdate_data(content)
{
    let success = false;
    try
    {
    getEleById("div_progress").style.display = "none";
    if (current_chip == null) {
        getEleById("btn_serwrite").disabled = true;
        throw new Error(`not enough data about ESC to proceed`);
    }
    if (!current_chip.hasOwnProperty("mcu")) {
        getEleById("btn_serwrite").disabled = true;
        throw new Error(`not enough data about ESC to proceed`);
    }
    getEleById("drop_selpin").disabled = true;
    getEleById("btn_connect").disabled = true;
    getEleById("btn_serwrite").disabled = true;
    getEleById("btn_fwupdate").disabled = true;
    let data;
    let pinnum = getEleById("drop_selpin").value;
    if (!isRunningLocally()) {
        let flash_length = current_chip["mcu"]["eeprom_start"] - current_chip["mcu"]["flash_start"];
        fwupdate_is_ongoing = true;
        await serport_ajax_flashWrite(content, current_chip["mcu"]["flash_start"], flash_length, flash_write_chunk, current_chip["mcu"]["addr_multi"], true);
        for (let retries = 3; retries >= 0; retries--)
        {
            try
            {
                data = await serport_ajax_flashRead(current_chip["mcu"]["flash_start"], flash_length, flash_write_chunk, current_chip["mcu"]["addr_multi"], true);
                fwupdate_is_ongoing = false;
                for (let i = 0; i < content.length; i++) {
                    if (content[i] != data[i]) {
                        throw new Error(`FW update verification failed at index ${i}`);
                    }
                }
                break; // all good, no need to retry
            }
            catch (e)
            {
                console.log("fwupdate_data serport_ajax_flashRead error: " + e.toString());
                if (retries == 0) {
                    throw e;
                }
            }
        }
        await serport_ajax_forceReboot();
    }
    else {
        getEleById("div_progress").style.display = "block";
        getEleById("div_progress").innerHTML = "pretend update";
        console.log("pretending to firmware update to pin " + pinnum);
        await new Promise(resolve => setTimeout(resolve, 2000));
        getEleById("div_progress").style.display = "none";
    }

    success = true;
    cuteAlert({
        type: 'success',
        title: 'Finished',
        message: 'Successfully finished firmware update'
    });

    }
    catch (e) {
        cuteAlert({
            type: 'error',
            title: 'Error during FW Update',
            message: e.toString() + "\r\n > Last Progress: " + getEleById("div_progress").innerHTML
        });
    }

    fwupdate_is_ongoing = false;

    getEleById("div_progress").style.display = "none";
    getEleById("btn_serwrite").style.display = "inline-block";
    getEleById("btn_serwrite").disabled = false;
    getEleById("drop_selpin").disabled = false;
    getEleById("btn_connect").disabled = false;
    getEleById("btn_fwupdate").disabled = false;

    await btn_connect_onclick_a();

    return success;
}

let fwupdate_filename = null;
let wakelock;
const canWakeLock = () => 'wakeLock' in navigator;

async function lockWakeState() {
    if (!canWakeLock()) return;
    try {
        wakelock = await navigator.wakeLock.request();
        wakelock.addEventListener('release', () => {
            console.log('Screen Wake State Locked:', !wakelock.released);
        });
        console.log('Screen Wake State Locked:', !wakelock.released);
    } catch (e) {
        console.error('Failed to lock wake state with reason:', e.message);
    }
}

function fwupdate(e)
{
    var file = e.target.files[0];
    if (!file) {
        getEleById("btn_fwupdate").value = "";
        cuteAlert({
            type: 'error',
            title: 'Error',
            message: 'file cannot be read'
        });
        return;
    }
    if (!isRunningLocally() && (current_chip == null || !current_chip.hasOwnProperty("mcu"))) {
        getEleById("btn_fwupdate").value = "";
        cuteAlert({
            type: 'error',
            title: 'Error',
            message: 'not enough data about ESC to proceed'
        });
        return;
    }

    lockWakeState();

    fwupdate_filename = file.name;
    var reader = new FileReader();
    reader.onload = function(e) {
        try {
            getEleById("div_progress").style.display = "block";
            getEleById("div_progress").innerHTML = "Preparing file...";
            let flash_start;
            let eeprom_start;
            if (!isRunningLocally()) {
                flash_start = current_chip["mcu"]["flash_start"];
                eeprom_start = current_chip["mcu"]["eeprom_start"];
            }
            else {
                flash_start = 0x1000;
                eeprom_start = 0x7C00;
            }
            let need_read_again = false;
            let memMap = MemoryMap.fromHex(e.target.result);
            let map_range = memMap.getRange();
            let start_addr = flash_start + (map_range[0] & 0xFFFF00000); // I hope this handles all chip variations
            let total_len = eeprom_start - flash_start;
            let total_len2 = map_range[1] - map_range[0];
            if (total_len2 > total_len) {
                // FW file seems to overlap with EEPROM
                total_len = total_len2;
                need_read_again = true;
            }
            else {
                total_len = total_len2; // keep the write short
            }
            let end_addr = start_addr + total_len;
            let fwArr = memMap.slicePad(start_addr, total_len);
            console.log("writing from 0x" + toHexString(start_addr) + " , len = 0x" + toHexString(total_len) + " (" + total_len + ") , ending at 0x" + toHexString(end_addr));
            fwupdate_data(fwArr).then(result => {
                if (result == false) {
                    getEleById("btn_fwupdate").value = "";
                    return;
                }
                if (current_chip != null && need_read_again == false) {
                    // V2 and V3 firmware do not contain the firmware_info_s structure metadata
                    // so I've resorted to simply checking the file name to see if an upgrade is being performed
                    let is_experimental = 0;
                    let regex = /[v_\-](\d+)/i;
                    let match = fwupdate_filename.match(regex);
                    if (match) {
                        let number = parseInt(match[1], 10);
                        if (number >= 3) {
                            is_experimental = number;
                        }
                    }
                    if (current_chip.hasOwnProperty("eeprom_0") && (current_chip["eeprom_0"] == 0 || current_chip["eeprom_0"] == 0xFF)) {
                        cuteAlert({
                            type: 'question',
                            title: 'Write EEPROM?',
                            message: `EEPROM appears blank, it's recommended that you immediate fill it with some data first`,
                            confirmText: 'Yes write EEPROM',
                            cancelText: 'No'
                        }).then((e)=>{
                            if (is_experimental > 0) {
                                getEleById("chk_experimentalupgrade").checked = true;
                                chk_experimentalupgrade_onchange();
                            }
                            btn_serwrite_onclick();
                        });
                    }
                    else if (is_experimental > 0 && (current_chip["eeprom_layout"] < 5 || current_chip["fw_version_major"] < 3)) {
                        cuteAlert({
                            type: 'question',
                            title: 'Upgrade EEPROM?',
                            message: `New file appears to be version ${is_experimental}, but ESC is reporting an older version ${current_chip["fw_version_major"]} (layout version ${current_chip["eeprom_layout"]}), you might need to upgrade the EEPROM version or else the ESC will not work.`,
                            confirmText: 'Yes Upgrade',
                            cancelText: 'No'
                        }).then((e)=>{
                            getEleById("chk_experimentalupgrade").checked = true;
                            chk_experimentalupgrade_onchange();
                            btn_serwrite_onclick();
                        });
                    }
                }
                else if (need_read_again)
                {
                    /*
                    cuteAlert({
                        type: 'info',
                        title: 'You should reboot',
                        message: `The new firmware file appears to also write EEPROM data, in this case, please power-cycle the ESC, and then read it again`
                    });
                    */
                }
                getEleById("btn_fwupdate").value = "";
            }).catch(er => {
                getEleById("div_progress").style.display = "none";
                cuteAlert({
                    type: 'error',
                    title: 'Error During FW Update',
                    message: er.toString() + "\r\n > Last Progress: " + getEleById("div_progress").innerHTML
                });
                console.log("error: exception while reading/sending firmware: " + er.toString());
            });
        }
        catch (ex) {
            getEleById("div_progress").style.display = "none";
            cuteAlert({
                type: 'error',
                title: 'Error During FW Update',
                message: ex.toString()
            });
            console.log("error: exception while reading/sending firmware: " + ex.toString());
        }
    };
    reader.readAsText(file);
}

let testesc_idleval = null;
let testesc_isactive = false;
let testesc_lastinputtime = null;
let testesc_istouched = false;
let testesc_tickrate = 20;

function register_test_params()
{
    // immediately after a read of the ESC or a write to the ESC, remember what kind of idle pulse setting is best
    try
    {
        // fist step is to see if the pin being used is configured for PWM/PPM or DSHOT
        // scan the text of the dropdown selected element
        const dropdown = getEleById("drop_selpin");
        if (!dropdown) {
            testesc_idleval = null;
            return;
        }
        const value = dropdown.value;
        let pin_text = null;

        const options = dropdown.options;
        for (let i = 0; i < options.length; i++) {
            if (options[i].value === value) {
                pin_text = options[i].text;
            }
        }

        if (pin_text == null) {
            console.log("cannot determine pin for testing ESC, pin not selected");
            testesc_idleval = null;
            return;
        }
        if (pin_text.startsWith("SER")) {
            // cannot test ESC if a serial TX pin is being used
            testesc_idleval = null;
            return;
        }

        let is_bidir = getEleById("chk_bidirectional").checked;
        if (pin_text.startsWith("DSHOT")) {
            // with DSHOT, AM32 completely ignores the "servo neutral" setting
            if (is_bidir) {
                testesc_idleval = 1500;
            }
            else {
                testesc_idleval = 1000;
            }
        }
        else {
            // with traditional PPM, AM32 will use the "servo neutral" and "servo low threshold" values as configured or calibrated
            if (is_bidir) {
                testesc_idleval = parseInt(getEleById("txt_servoneutral").value);
            }
            else {
                testesc_idleval = parseInt(getEleById("txt_servolowthresh").value);;
            }
        }
    }
    catch (e) {
        console.log("error in function \"register_test_params\": " + e.toString());
    }
}

let input_element_disabled_cache = {};

function testesc_cache_disabled() {
    // remember which elements were previously already disabled
    const elements = document.querySelectorAll('input, select');
    elements.forEach(element => {
        if (element.id) {
            input_element_disabled_cache[element.id] = element.disabled;
        }
    });
}

function testesc_restore() {
    // return the disabled/enabled state of all inputs back to when test mode was not active
    const elements = document.querySelectorAll('input, select');
    elements.forEach(element => {
        if (element.id) {
            if (element.id == "btn_testesc" || element.id == "sld_testvalue") {
                return;
            }
            if (input_element_disabled_cache.hasOwnProperty(element.id)) {
                element.disabled = input_element_disabled_cache[element.id];
            }
        }
    });
    getEleById("sld_testvalue").disabled = true;
}

function testesc_disable_rest() {
    // disable all input elements on the whole page, but remember which ones were already disabled
    // the previous state will be restored later
    testesc_cache_disabled();
    const elements = document.querySelectorAll('input, select');
    elements.forEach(element => {
        if (element.id == "btn_testesc" || element.id == "sld_testvalue") {
            return;
        }
        if (element.id) {
            element.disabled = true;
        }
    });
}

async function testesc_start()
{
    if (testesc_idleval == null) {
        cuteAlert({
            type: 'error',
            title: 'Error',
            message: 'unable to start test with current configuration'
        });
        testesc_stop();
        return;
    }
    try {
    testesc_errorcnt = 0;
    getEleById('sld_testvalue').value = testesc_idleval;
    if (!isRunningLocally()) {
        let pinnum = parseInt(getEleById("drop_selpin").value);
        pin_been_tried[pinnum] = false;
        let resp = await serport_ajax("starting test", srvaction_test_start, pinnum);
        if (resp != "ok") {
            cuteAlert({
                type: 'error',
                title: 'Error',
                message: 'unable to start test, cannot initialize pin'
            });
            testesc_stop();
            return;
        }
    }
    else {
        testesc_tickrate = 200;
    }
    getEleById("btn_testesc").value = "Stop Test";
    testesc_disable_rest();
    getEleById("sld_testvalue").disabled = false;
    testesc_isactive = true;
    sld_testvalue_onchange();
    setTimeout(testesc_tick, testesc_tickrate);
    console.log("ESC test started");
    }
    catch (e) {
        console.log("failed to start test mode: " + e.toString());
        cuteAlert({
            type: 'error',
            title: 'Error',
            message: 'Exception occured when starting test mode: ' + e.toString()
        });
        testesc_stop();
    }
}

async function testesc_stop()
{
    let need_reconnect = testesc_isactive;
    if (testesc_idleval != null) {
        getEleById('sld_testvalue').value = testesc_idleval;
        sld_testvalue_onchange();
        await testesc_tick_a();
    }
    getEleById("btn_testesc").value = "Start Test";
    testesc_restore();
    getEleById("sld_testvalue").disabled = true;
    testesc_isactive = false;
    console.log("ESC test stopped");
    if (need_reconnect) {
        await btn_connect_onclick_a();
    }
}

function btn_testesc_onclick() {
    let btn_ele = getEleById("btn_testesc");
    if (testesc_isactive == false) {
        testesc_start();
    }
    else {
        testesc_stop();
    }
}

let testesc_errorcnt = 0;

async function testesc_tick_a() {
    if (testesc_isactive == false) {
        return;
    }

    let now = new Date();
    const nowTime = now.getTime();
    const pastTime = testesc_lastinputtime.getTime();
    const differenceInMilliseconds = nowTime - pastTime;
    if ((testesc_istouched == false && differenceInMilliseconds >= 3000) || differenceInMilliseconds >= 8000) {
        // failsafe on no activity
        getEleById('sld_testvalue').value = testesc_idleval;
        getEleById('div_testvalue').innerHTML = testesc_idleval;
        console.log("ESC test is idle " + testesc_idleval);
    }

    let v = getEleById('sld_testvalue').value;
    try {
        if (!isRunningLocally()) {
            await serport_ajax("send test pulse", srvaction_test_pulse, serport_lastpin, null, v);
        }
        else {
            console.log("ESC test pulse " + v);
        }
        testesc_errorcnt = 0;
    }
    catch (e) {
        console.log("error while sending test pulse: " + e.toString());
        testesc_errorcnt += 1;
        if (testesc_errorcnt > 2) { // too many errors, each error actually takes quite a while, so this threshold is low
            cuteAlert({
                type: 'error',
                title: 'Error',
                message: 'Too many errors occured when trying to send test pulse data, exception message: ' + e.toString()
            });
            testesc_errorcnt = 0; // this will run again, prevent a second message
            testesc_stop();
            return;
        }
    }

    setTimeout(testesc_tick, testesc_tickrate);
}

function testesc_tick() {
    testesc_tick_a();
}

function sld_testvalue_oninput() {
    // oninput is called when the slider is being moved actively, the user is dragging
    testesc_lastinputtime = new Date();
    let v = getEleById('sld_testvalue').value;
    getEleById('div_testvalue').innerHTML = v;
    testesc_istouched = true;
}

function sld_testvalue_onchange() {
    // onchange is called when the user releases the input
    testesc_lastinputtime = new Date();
    let v = getEleById('sld_testvalue').value;
    getEleById('div_testvalue').innerHTML = v;
    testesc_istouched = false;
}

// NOTE: The legacy build used a preprocessor include for shared utilities.
// In the Lit/Vite migration, `cuteAlert` is injected from feedback.js by am32-panel.

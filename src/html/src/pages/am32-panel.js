import {html, LitElement} from 'lit'
import {customElement} from 'lit/decorators.js'
import {cuteAlert} from '../utils/feedback.js'
import {elrsState} from '../utils/state.js'

// Keep one shared script-loader promise so we do not append duplicate <script> tags
// when users navigate away from and back to this route.
let legacyAm32ScriptsReadyPromise = null
const EXTRA_FEATURE_AM32_BIT = 4

@customElement('am32-panel')
export class Am32Panel extends LitElement {
    createRenderRoot() {
        return this
    }

    async firstUpdated() {
        if (!this._isFeatureAvailable()) {
            return
        }

        try {
            await this._ensureLegacyScriptsLoaded()

            // The legacy AM32 runtime expects `cuteAlert` in the global scope because
            // historical builds injected it via @@include("libs.js").
            // We provide the modern implementation from feedback.js here.
            if (typeof window.cuteAlert !== 'function') {
                window.cuteAlert = cuteAlert
            }

            if (typeof window.am32_init === 'function') {
                // The legacy implementation expects all DOM IDs to exist before init.
                // firstUpdated runs after render, so this is the safest moment to invoke it.
                window.am32_init()
            } else {
                const loadingElement = document.getElementById('div_loading')
                if (loadingElement) {
                    loadingElement.textContent = 'AM32 legacy script loaded, but am32_init() was not found.'
                }
            }
        } catch (error) {
            const loadingElement = document.getElementById('div_loading')
            if (loadingElement) {
                loadingElement.textContent = `Failed to load AM32 configurator: ${error.message}`
            }
        }
    }

    async _ensureLegacyScriptsLoaded() {
        if (!legacyAm32ScriptsReadyPromise) {
            legacyAm32ScriptsReadyPromise = (async () => {
                await this._loadClassicScript('/am32/ihex.js')
                await this._loadClassicScript('/am32/am32.js')
            })()
        }
        return legacyAm32ScriptsReadyPromise
    }

    _loadClassicScript(scriptUrl) {
        return new Promise((resolve, reject) => {
            const existingElement = document.querySelector(`script[src="${scriptUrl}"]`)
            if (existingElement) {
                if (existingElement.dataset.loaded === 'true') {
                    resolve()
                    return
                }

                existingElement.addEventListener('load', () => resolve(), {once: true})
                existingElement.addEventListener('error', () => reject(new Error(`Unable to load ${scriptUrl}`)), {once: true})
                return
            }

            const scriptElement = document.createElement('script')
            scriptElement.src = scriptUrl
            scriptElement.async = false
            scriptElement.addEventListener('load', () => {
                scriptElement.dataset.loaded = 'true'
                resolve()
            }, {once: true})
            scriptElement.addEventListener('error', () => reject(new Error(`Unable to load ${scriptUrl}`)), {once: true})
            document.head.appendChild(scriptElement)
        })
    }

    render() {
        if (!this._isFeatureAvailable()) {
            return html`
                <div class="mui-panel mui--text-title">AM32 Configurator</div>
                <div class="mui-panel">
                    <p>This firmware does not support this feature.</p>
                </div>
            `
        }

        return html`
            <div class="mui-panel mui--text-title">AM32 Configurator</div>
            <div id="div_loading">Loading... Please Wait...</div>
            <div id="div_escconnect" style="display: none;" class="mui-panel">
                <fieldset>
                    <label for="drop_selpin">Select the pin:</label>
                    <select id="drop_selpin" name="drop_selpin"></select>
                    <input type="button" id="btn_connect" value="Connect and Read" onclick="btn_connect_onclick()" />
                    <input type="button" id="btn_serwrite" value="Write to ESC" onclick="btn_serwrite_onclick()" />
                    <span id="span_pleasewait" style="display:none;">Please Wait...</span>
                </fieldset>
            </div>
            <div id="div_maincontent" style="display: none;" class="mui-panel">
                <fieldset><legend>Options</legend>
                    <div>
                        <div style="display:inline-block; vertical-align:top;">
                            <div style="display: table; padding-right:30pt;">
                                <div style="display: table-row-group;" id="tbl_checkboxes"></div>
                                <div style="display: table-row-group;" id="tbl_checkboxes_219"></div>
                                <div>
                                    <fieldset><legend>RC Input</legend>
                                        <select id="drop_rcinput" onchange="drop_rcinput_onchange()">
                                            <option value="x_0">Automatic</option>
                                            <option value="x_1">DShot</option>
                                            <option value="x_2">Servo</option>
                                            <option value="x_3">Serial</option>
                                            <option value="x_4">EDTARM</option>
                                            <option value="x_5">??? 5</option>
                                            <option value="x_6">??? 6</option>
                                            <option value="x_7">??? 7</option>
                                            <option value="x_8">??? 8</option>
                                            <option value="x_9">??? 9</option>
                                        </select>
                                        <fieldset id="fld_crsfchannels"><legend>CRSF Channel</legend>
                                            <input type="number" id="txt_crsfchannel" name="txt_crsfchannel" value="1" step="1" min="1" max="8" />
                                        </fieldset>
                                        <fieldset id="fld_crsf2channels"><legend>CRSF Secondary Channel</legend>
                                            <input type="number" id="txt_crsf2channel" name="txt_crsf2channel" value="1" step="1" min="1" max="8" />
                                        </fieldset>
                                    </fieldset>
                                </div>
                            </div>
                        </div>
                        <div style="display:inline-block; vertical-align:top;">
                            <div style="display: table;">
                                <div style="display: table-row-group;" id="tbl_sliders"></div>
                            </div>
                            <div style="display: table;">
                                <div style="display: table-row-group;" id="tbl_sliders_219"></div>
                            </div>
                        </div>
                    </div>
                    <br />
                    <div>
                        <div style="display:inline-block; padding-right:1em">
                            <fieldset><legend><label for="txt_devicename">Device Name</label></legend>
                                <input type="text" id="txt_devicename" maxlength="12" />
                            </fieldset>
                        </div>
                        <div style="display:inline-block;">
                            <fieldset><legend><label for="txt_devicename">FW Version</label></legend>
                                <span id="span_firmwareversion">V0.0 EL0 BL0</span><span id="span_experimentalupgrade"><br /><input type="checkbox" id="chk_experimentalupgrade" onchange="chk_experimentalupgrade_onchange()" /><label for="chk_experimentalupgrade">Force upgrade to experimental EEPROM?</label></span>
                            </fieldset>
                        </div>
                    </div>
                    <div id="div_experimentalextras">
                        <fieldset><legend>Experimental Extras</legend>
                            <div>
                                <div style="display:inline-block; vertical-align:top;">
                                    <div style="display: table; padding-right:30pt;">
                                        <div style="display: table-row-group;" id="tbl_extracheckboxes"> </div>
                                    </div>
                                </div>
                                <div style="display:inline-block; vertical-align:top;">
                                    <div style="display: table;">
                                        <div style="display: table-row-group;" id="tbl_extrasliders"></div>
                                    </div>
                                </div>
                            </div>
                        </fieldset>
                    </div>
                </fieldset>
                <div style="display: table; width: 100%;">
                    <div style="display: table-row-group;">
                        <div style="display: table-row;">
                            <div style="display: table-cell; width: 50%;">
                                <fieldset><legend>Open Cfg File</legend>
                                    <input type="file" id="btn_readbinfile" />
                                </fieldset>
                            </div>
                            <div style="display: table-cell;">
                                <fieldset><legend>Save Cfg File</legend>
                                    <input type="button" value="Save" onclick="saveBinFile()" />&nbsp;<input type="text" value="" id="txt_savefname" placeholder="optional name" />
                                </fieldset>
                            </div>
                        </div>
                    </div>
                </div>
                <fieldset id="fld_fwupdate"><legend>FW Update (*.HEX file)</legend>
                    <div><input type="file" id="btn_fwupdate" /></div>
                    <div style="font-size: 8pt;"><b>WARNING: </b> this can take many minutes, please ensure that your power is stable, Wi-Fi connection is reliable, and your computer/phone does not go to sleep.</div>
                    <div id="div_progress"></div>
                </fieldset>
            </div>
            <div id="div_testesc" style="display: none;" class="mui-panel">
                <fieldset id="fld_testesc"><legend>Test ESC</legend>
                    <div style="display: table; width: 100%;">
                        <div style="display: table-row-group;">
                            <div style="display: table-row;">
                                <div style="display: table-cell; width: 50%;">
                                    <input type="button" id="btn_testesc" value="Start Test" onclick="btn_testesc_onclick()" />
                                </div>
                                <div style="display: table-cell;">
                                    <div id="div_testvalue"></div>
                                </div>
                            </div>
                        </div>
                    </div>
                    <div style="padding-top:1em;padding-bottom:1em;"><input type="range" id="sld_testvalue" min="1000" max="2000" style="width: 100%;" oninput="sld_testvalue_oninput()" onchange="sld_testvalue_oninput()" onfocus="sld_testvalue_oninput()" onmousedown="sld_testvalue_oninput()" /></div>
                </fieldset>
            </div>
        `
    }

    _isFeatureAvailable() {
        const extraFeatureFlags = Number(elrsState.settings['extra-features-avail']) || 0
        return (extraFeatureFlags & (1 << EXTRA_FEATURE_AM32_BIT)) !== 0
    }
}

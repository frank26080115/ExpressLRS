import {html, LitElement} from "lit";
import {customElement, state} from "lit/decorators.js";
import {elrsState, saveConfig} from "../utils/state.js";
import {cuteAlert, post, showAlert} from "../utils/feedback.js";

const BP_OCCUPANCY_LEFT_STICK = 1 << 0;
const BP_OCCUPANCY_RIGHT_STICK = 1 << 1;
const BP_OCCUPANCY_DPAD = 1 << 2;
const BP_OCCUPANCY_LEFT_TRIGGER = 1 << 3;
const BP_OCCUPANCY_RIGHT_TRIGGER = 1 << 4;
const BP_OCCUPANCY_FACE_A = 1 << 5;
const BP_OCCUPANCY_FACE_B = 1 << 6;
const BP_OCCUPANCY_FACE_X = 1 << 7;
const BP_OCCUPANCY_FACE_Y = 1 << 8;
const BP_OCCUPANCY_L1 = 1 << 9;
const BP_OCCUPANCY_R1 = 1 << 10;

const OCCUPANCY_LABELS = [
    [BP_OCCUPANCY_LEFT_STICK, 'left stick'],
    [BP_OCCUPANCY_RIGHT_STICK, 'right stick'],
    [BP_OCCUPANCY_DPAD, 'D-pad'],
    [BP_OCCUPANCY_LEFT_TRIGGER, 'left trigger'],
    [BP_OCCUPANCY_RIGHT_TRIGGER, 'right trigger'],
    [BP_OCCUPANCY_FACE_A, 'A button'],
    [BP_OCCUPANCY_FACE_B, 'B button'],
    [BP_OCCUPANCY_FACE_X, 'X button'],
    [BP_OCCUPANCY_FACE_Y, 'Y button'],
    [BP_OCCUPANCY_L1, 'L1 button'],
    [BP_OCCUPANCY_R1, 'R1 button'],
];

const MAIN_MODE_OPTIONS = [
    {label: 'Flat Mapping', occupancy: 0},
    {label: 'Both Sticks Fully Automatic', occupancy: BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_RIGHT_STICK},
    {label: 'Both Sticks Direct Y Control', occupancy: BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_RIGHT_STICK},
    {label: 'Only Left Stick', occupancy: BP_OCCUPANCY_LEFT_STICK},
    {label: 'Only Right Stick', occupancy: BP_OCCUPANCY_RIGHT_STICK},
    {label: 'Only D-pad', occupancy: BP_OCCUPANCY_DPAD},
    {label: 'Left Stick Throttle, Right Stick Steering', occupancy: BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_RIGHT_STICK},
    {label: 'Right Stick Throttle, Left Stick Steering', occupancy: BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_RIGHT_STICK},
    {label: 'Racing: Trigger for Throttle, Left Stick Steering', occupancy: BP_OCCUPANCY_LEFT_STICK | BP_OCCUPANCY_LEFT_TRIGGER | BP_OCCUPANCY_RIGHT_TRIGGER},
    {label: 'Racing: Trigger for Throttle, Right Stick Steering', occupancy: BP_OCCUPANCY_RIGHT_STICK | BP_OCCUPANCY_LEFT_TRIGGER | BP_OCCUPANCY_RIGHT_TRIGGER},
];

const ANALOG_MODE_OPTIONS = [
    {label: 'Do Nothing, Uses Buttons', occupancy: 0},
    {label: 'Left Stick Y Mapped Directly', occupancy: BP_OCCUPANCY_LEFT_STICK},
    {label: 'Right Stick Y Mapped Directly', occupancy: BP_OCCUPANCY_RIGHT_STICK},
    {label: 'Left Stick Y Relative Changes', occupancy: BP_OCCUPANCY_LEFT_STICK},
    {label: 'Right Stick Y Relative Changes', occupancy: BP_OCCUPANCY_RIGHT_STICK},
    {label: 'Left Trigger Mapped Directly', occupancy: BP_OCCUPANCY_LEFT_TRIGGER},
    {label: 'Right Trigger Mapped Directly', occupancy: BP_OCCUPANCY_RIGHT_TRIGGER},
    {label: 'Left Trigger Lowers, Right Trigger Raises', occupancy: BP_OCCUPANCY_LEFT_TRIGGER | BP_OCCUPANCY_RIGHT_TRIGGER},
    {label: 'Right Trigger Lowers, Left Trigger Raises', occupancy: BP_OCCUPANCY_LEFT_TRIGGER | BP_OCCUPANCY_RIGHT_TRIGGER},
];

const BUTTON_MODE_OPTIONS = [
    'Do Nothing',
    'Tap to Set and Latch',
    'Set While Held, Release to Failsafe',
    'Tap to Increment',
    'Tap to Decrement',
];

const BUTTONS = [
    {label: 'Face A', occupancy: BP_OCCUPANCY_FACE_A},
    {label: 'Face B', occupancy: BP_OCCUPANCY_FACE_B},
    {label: 'Face X', occupancy: BP_OCCUPANCY_FACE_X},
    {label: 'Face Y', occupancy: BP_OCCUPANCY_FACE_Y},
    {label: 'D-pad Up', occupancy: BP_OCCUPANCY_DPAD},
    {label: 'D-pad Down', occupancy: BP_OCCUPANCY_DPAD},
    {label: 'D-pad Left', occupancy: BP_OCCUPANCY_DPAD},
    {label: 'D-pad Right', occupancy: BP_OCCUPANCY_DPAD},
    {label: 'L1', occupancy: BP_OCCUPANCY_L1},
    {label: 'R1', occupancy: BP_OCCUPANCY_R1},
];

const CHANNEL_OPTIONS = ['Unused', 'CH1', 'CH2', 'CH3', 'CH4', 'CH5', 'CH6', 'CH7', 'CH8', 'CH9', 'CH10', 'CH11', 'CH12', 'CH13', 'CH14', 'CH15', 'CH16'];
const PAIRED_DEVICE_POLL_MS = 3000;

@customElement('bluepad-panel')
class BluepadPanel extends LitElement {
    @state() accessor devices = [];
    @state() accessor pairingEnabled = false;
    @state() accessor showDeviceLoader = true;

    bluepadConfig = {};
    running = false;
    pollTimer = null;

    constructor() {
        super();
        this.getPairedDevices = this.getPairedDevices.bind(this);
    }

    createRenderRoot() {
        this.loadBluepadConfigFromState();
        return this;
    }

    disconnectedCallback() {
        super.disconnectedCallback();
        this.running = false;
        if (this.pollTimer) {
            clearTimeout(this.pollTimer);
            this.pollTimer = null;
        }
    }

    updated(_) {
        if (!this.running) {
            this.running = true;
            this.getPairedDevices();
        }
    }

    render() {
        if (!this.isFeatureAvailable()) {
            return html`
                <div class="mui-panel mui--text-title">Bluepad32</div>
                <div class="mui-panel">
                    <p>This firmware does not expose Bluepad32 configuration.</p>
                </div>
            `;
        }

        const warnings = this.validationWarnings();

        return html`
            <div class="mui-panel mui--text-title">Bluepad32</div>
            <div class="mui-panel">
                <p>
                    Configure Bluetooth gamepad pairing, manage paired controllers, and map controller inputs onto
                    the main controls and two auxiliary channels.
                </p>
                ${this.renderPairingControls()}
                ${this.renderConfigEditor(warnings)}
            </div>
        `;
    }

    renderPairingControls() {
        return html`
            <h3>Pairing</h3>
            <div class="mui-panel info-bg">
                Pairing is currently <b>${this.pairingEnabled ? 'enabled' : 'disabled'}</b>.
                Previously paired controllers may reconnect even while pairing is disabled.
            </div>
            <div>
                <button class="mui-btn mui-btn--primary" ?disabled="${this.pairingEnabled}" @click="${(event) => this.setPairing(event, true)}">Enable Pairing</button>
                <button class="mui-btn" ?disabled="${!this.pairingEnabled}" @click="${(event) => this.setPairing(event, false)}">Disable Pairing</button>
                <button class="mui-btn mui-btn--danger" ?disabled="${this.devices.length === 0}" @click="${(event) => this.deleteAllDevices(event)}">Forget All Devices</button>
            </div>
            <h4>Paired Devices</h4>
            ${this.showDeviceLoader ? html`<div class="loader"></div>` : ''}
            ${this.devices.length === 0 ? html`
                <p>No paired BR/EDR devices found.</p>
            ` : html`
                <table class="mui-table">
                    <thead>
                        <tr>
                            <th>BD Address</th>
                            <th>Key Type</th>
                            <th></th>
                        </tr>
                    </thead>
                    <tbody>
                        ${this.devices.map((device) => html`
                            <tr>
                                <td>${device.address}</td>
                                <td>${device.type}</td>
                                <td><button class="mui-btn mui-btn--small mui-btn--danger" @click="${(event) => this.deleteDevice(event, device.address)}">Delete</button></td>
                            </tr>
                        `)}
                    </tbody>
                </table>
            `}
        `;
    }

    renderConfigEditor(warnings) {
        return html`
            <h3>Configuration</h3>
            <form class="mui-form">
                <div class="mui-select">
                    <select @change="${(event) => this.updateMainMode(event.target.value)}">
                        ${MAIN_MODE_OPTIONS.map((option, index) => html`
                            <option value="${index}" ?selected="${index === this.bluepadConfig.main_mode}">${option.label}</option>
                        `)}
                    </select>
                    <label>Main Analog Control Mode</label>
                </div>

                ${this.renderAuxTable()}
                ${this.renderButtonTable()}
                ${this.renderWarnings(warnings)}

                <button class="mui-btn mui-btn--primary" @click="${(event) => this.save(event)}">Save</button>
            </form>
        `;
    }

    renderAuxTable() {
        return html`
            <h4>Auxiliary Channels</h4>
            <table class="mui-table">
                <thead>
                    <tr>
                        <th>Aux</th>
                        <th>Actual Channel</th>
                        <th>Failsafe</th>
                        <th>Analog Mode</th>
                    </tr>
                </thead>
                <tbody>
                    ${this.bluepadConfig.aux_mode.map((aux, index) => html`
                        <tr>
                            <td>AUX ${index + 1}</td>
                            <td>${this.renderChannelSelect(aux.actual_channel, (value) => this.updateAux(index, 'actual_channel', value))}</td>
                            <td>${this.renderFailsafeInput(aux, index)}</td>
                            <td>${this.renderAnalogSelect(aux.analog_mode, (value) => this.updateAux(index, 'analog_mode', value))}</td>
                        </tr>
                    `)}
                </tbody>
            </table>
        `;
    }

    renderButtonTable() {
        return html`
            <h4>Buttons for Auxiliary</h4>
            <table class="mui-table">
                <thead>
                    <tr>
                        <th>Button</th>
                        <th>Aux</th>
                        <th>Mode</th>
                        <th>Value / Step</th>
                    </tr>
                </thead>
                <tbody>
                    ${this.bluepadConfig.btn_mode.map((button, index) => html`
                        <tr>
                            <td>${BUTTONS[index].label}</td>
                            <td>
                                <div class="mui-select compact">
                                    <select @change="${(event) => this.updateButton(index, 'aux_chan', event.target.value)}">
                                        <option value="0" ?selected="${button.aux_chan === 0}">AUX 1</option>
                                        <option value="1" ?selected="${button.aux_chan === 1}">AUX 2</option>
                                    </select>
                                </div>
                            </td>
                            <td>
                                <div class="mui-select compact">
                                    <select @change="${(event) => this.updateButton(index, 'mode', event.target.value)}">
                                        ${BUTTON_MODE_OPTIONS.map((label, modeIndex) => html`
                                            <option value="${modeIndex}" ?selected="${modeIndex === button.mode}">${label}</option>
                                        `)}
                                    </select>
                                </div>
                            </td>
                            <td>
                                <div class="mui-textfield compact">
                                    <input type="number" min="0" max="2500" .value="${String(button.value)}" @change="${(event) => this.updateButton(index, 'value', event.target.value)}">
                                </div>
                            </td>
                        </tr>
                    `)}
                </tbody>
            </table>
        `;
    }

    renderChannelSelect(value, onChange) {
        return html`
            <div class="mui-select compact">
                <select @change="${(event) => onChange(event.target.value)}">
                    ${CHANNEL_OPTIONS.map((label, index) => html`
                        <option value="${index}" ?selected="${index === value}">${label}</option>
                    `)}
                </select>
            </div>
        `;
    }

    renderAnalogSelect(value, onChange) {
        return html`
            <div class="mui-select compact">
                <select @change="${(event) => onChange(event.target.value)}">
                    ${ANALOG_MODE_OPTIONS.map((option, index) => html`
                        <option value="${index}" ?selected="${index === value}">${option.label}</option>
                    `)}
                </select>
            </div>
        `;
    }

    renderFailsafeInput(aux, index) {
        if (!this.usesTxFailsafe()) {
            return html`<span>PWM failsafe</span>`;
        }

        return html`
            <div class="mui-textfield compact">
                <input type="number" min="750" max="2250" .value="${String(aux.failsafe)}" @change="${(event) => this.updateAux(index, 'failsafe', event.target.value)}">
            </div>
        `;
    }

    renderWarnings(warnings) {
        return html`
            <div class="mui-panel ${warnings.length ? 'warning-bg' : 'info-bg'}">
                ${warnings.length ? html`
                    <b>Warnings</b>
                    <ul>
                        ${warnings.map((warning) => html`<li>${warning}</li>`)}
                    </ul>
                ` : 'No mapping conflicts detected.'}
            </div>
        `;
    }

    setPairing(event, enabled) {
        event.preventDefault();
        post(enabled ? '/bluepad/pairing/enable' : '/bluepad/pairing/disable', {}, {
            onload: (xhr) => {
                this.applyDeviceResponse(xhr);
                this.pairingEnabled = enabled;
                this.getPairedDevices(true);
            },
            onerror: async (xhr) => {
                await showAlert('error', 'Bluepad32 Pairing', xhr.responseText || 'Pairing request failed');
            },
        });
    }

    async deleteDevice(event, address) {
        event.preventDefault();
        const result = await cuteAlert({
            type: 'question',
            title: 'Delete Paired Device',
            message: `Forget ${address}? The controller will need to pair again.`,
            confirmText: 'Delete',
            cancelText: 'Cancel',
        });
        if (result !== 'confirm') {
            return;
        }

        post(`/bluepad/devices/delete?addr=${encodeURIComponent(address)}`, {}, {
            onload: (xhr) => {
                this.applyDeviceResponse(xhr);
                this.getPairedDevices(true);
            },
            onerror: async (xhr) => {
                await showAlert('error', 'Delete Paired Device', xhr.responseText || 'Delete request failed');
            },
        });
    }

    async deleteAllDevices(event) {
        event.preventDefault();
        const result = await cuteAlert({
            type: 'question',
            title: 'Forget All Devices',
            message: 'Forget every paired controller? Controllers will need to pair again.',
            confirmText: 'Forget All',
            cancelText: 'Cancel',
        });
        if (result !== 'confirm') {
            return;
        }

        post('/bluepad/devices/delete-all', {}, {
            onload: (xhr) => {
                this.applyDeviceResponse(xhr);
                this.getPairedDevices(true);
            },
            onerror: async (xhr) => {
                await showAlert('error', 'Forget All Devices', xhr.responseText || 'Forget request failed');
            },
        });
    }

    getPairedDevices(immediate = false) {
        if (!this.running) {
            return;
        }

        if (this.pollTimer) {
            clearTimeout(this.pollTimer);
            this.pollTimer = null;
        }

        const request = new XMLHttpRequest();
        request.onload = () => {
            if (!this.running) {
                return;
            }
            if (request.status === 200) {
                this.applyDeviceResponse(request);
                this.showDeviceLoader = false;
            }
            this.scheduleDevicePoll();
        };
        request.onerror = () => {
            if (this.running) {
                this.scheduleDevicePoll();
            }
        };
        request.open('GET', '/bluepad/devices.json', true);
        request.send();

        if (immediate) {
            this.showDeviceLoader = true;
        }
    }

    scheduleDevicePoll() {
        if (!this.running) {
            return;
        }
        this.pollTimer = setTimeout(this.getPairedDevices, PAIRED_DEVICE_POLL_MS);
    }

    applyDeviceResponse(xhr) {
        try {
            const data = JSON.parse(xhr.responseText || '{}');
            if (Array.isArray(data.devices)) {
                this.devices = data.devices;
            }
            if (typeof data.pairing === 'boolean') {
                this.pairingEnabled = data.pairing;
            }
        } catch (_) {
        }
    }

    updateMainMode(value) {
        this.bluepadConfig.main_mode = this.toInt(value, 0, MAIN_MODE_OPTIONS.length - 1, 0);
        this.requestUpdate();
    }

    updateAux(index, key, value) {
        const aux = this.bluepadConfig.aux_mode[index];
        if (key === 'actual_channel') {
            aux.actual_channel = this.toInt(value, 0, 16, 0);
        } else if (key === 'analog_mode') {
            aux.analog_mode = this.toInt(value, 0, ANALOG_MODE_OPTIONS.length - 1, 0);
        } else if (key === 'failsafe') {
            aux.failsafe = this.toInt(value, 750, 2250, 988);
        }
        this.requestUpdate();
    }

    updateButton(index, key, value) {
        const button = this.bluepadConfig.btn_mode[index];
        if (key === 'aux_chan') {
            button.aux_chan = this.toInt(value, 0, 1, 0);
        } else if (key === 'mode') {
            button.mode = this.toInt(value, 0, BUTTON_MODE_OPTIONS.length - 1, 0);
        } else if (key === 'value') {
            button.value = this.toInt(value, 0, 2500, 0);
        }
        this.requestUpdate();
    }

    save(event) {
        event.preventDefault();
        const sanitizedConfig = this.sanitizeConfig(this.bluepadConfig);
        saveConfig({
            bluepad: sanitizedConfig,
        }, () => {
            this.bluepadConfig = sanitizedConfig;
            this.requestUpdate();
        });
    }

    validationWarnings() {
        const warnings = [];
        const occupiedBy = new Map();
        const claim = (mask, source) => {
            for (const [flag, label] of OCCUPANCY_LABELS) {
                if ((mask & flag) === 0) {
                    continue;
                }
                const previousSource = occupiedBy.get(flag);
                if (previousSource) {
                    warnings.push(`${source} also uses ${label}, already used by ${previousSource}.`);
                } else {
                    occupiedBy.set(flag, source);
                }
            }
        };

        claim(MAIN_MODE_OPTIONS[this.bluepadConfig.main_mode]?.occupancy || 0, 'Main mode');

        const channels = new Map();
        this.bluepadConfig.aux_mode.forEach((aux, index) => {
            const label = `AUX ${index + 1}`;
            claim(ANALOG_MODE_OPTIONS[aux.analog_mode]?.occupancy || 0, `${label} analog mode`);

            if (aux.actual_channel > 0) {
                const previousAux = channels.get(aux.actual_channel);
                if (previousAux) {
                    warnings.push(`${label} and ${previousAux} both target CH${aux.actual_channel}.`);
                } else {
                    channels.set(aux.actual_channel, label);
                }
            }

            if (this.usesTxFailsafe()) {
                if ((aux.analog_mode === 1 || aux.analog_mode === 2) && (aux.failsafe < 1400 || aux.failsafe > 1600)) {
                    warnings.push(`${label} direct stick mode expects a failsafe near 1500.`);
                } else if (aux.analog_mode >= 3 && aux.analog_mode <= 8 && aux.failsafe > 1100) {
                    warnings.push(`${label} relative/trigger mode usually expects a low failsafe near 988.`);
                }
            }
        });

        this.bluepadConfig.btn_mode.forEach((button, index) => {
            if (button.mode === 0) {
                return;
            }
            claim(BUTTONS[index].occupancy, `${BUTTONS[index].label} button mode`);
        });

        return warnings;
    }

    checkChanged() {
        if (!this.isFeatureAvailable()) {
            return false;
        }
        return JSON.stringify(this.sanitizeConfig(this.bluepadConfig)) !== JSON.stringify(this.sanitizeConfig(elrsState.config.bluepad));
    }

    loadBluepadConfigFromState() {
        this.bluepadConfig = this.sanitizeConfig(elrsState.config.bluepad);
    }

    sanitizeConfig(inputConfig) {
        const source = inputConfig || {};
        const auxModes = Array.isArray(source.aux_mode) ? source.aux_mode : [];
        const buttonModes = Array.isArray(source.btn_mode) ? source.btn_mode : [];
        return {
            main_mode: this.toInt(source.main_mode, 0, MAIN_MODE_OPTIONS.length - 1, 0),
            aux_mode: [0, 1].map((index) => this.sanitizeAux(auxModes[index] || {})),
            btn_mode: BUTTONS.map((_, index) => this.sanitizeButton(buttonModes[index] || {})),
        };
    }

    sanitizeAux(inputAux) {
        const aux = {
            actual_channel: this.toInt(inputAux.actual_channel, 0, 16, 0),
            analog_mode: this.toInt(inputAux.analog_mode, 0, ANALOG_MODE_OPTIONS.length - 1, 0),
        };
        if (this.usesTxFailsafe() || inputAux.failsafe !== undefined) {
            aux.failsafe = this.toInt(inputAux.failsafe, 750, 2250, 988);
        }
        return aux;
    }

    sanitizeButton(inputButton) {
        return {
            aux_chan: this.toInt(inputButton.aux_chan, 0, 1, 0),
            mode: this.toInt(inputButton.mode, 0, BUTTON_MODE_OPTIONS.length - 1, 0),
            value: this.toInt(inputButton.value, 0, 2500, 0),
        };
    }

    usesTxFailsafe() {
        return elrsState.settings['module-type'] === 'TX';
    }

    isFeatureAvailable() {
        return elrsState.config.bluepad !== undefined;
    }

    toInt(value, minimum, maximum, fallback) {
        const parsed = Number.parseInt(value, 10);
        if (Number.isNaN(parsed)) {
            return fallback;
        }
        if (parsed < minimum) {
            return minimum;
        }
        if (parsed > maximum) {
            return maximum;
        }
        return parsed;
    }
}

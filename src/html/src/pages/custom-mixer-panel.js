import {html, LitElement} from "lit";
import {customElement} from "lit/decorators.js";
import {elrsState, saveConfig} from "../utils/state.js";

const SCALE_STEP_PERCENT = 5;
const EXTRA_FEATURE_CUSTOM_MIXER_BIT = 1;
const DEFAULT_CHANNEL_OPTIONS = ['None/Disabled', 'CH1', 'CH2', 'CH3', 'CH4', 'CH5', 'CH6', 'CH7', 'CH8', 'CH9', 'CH10', 'CH11', 'CH12', 'CH13', 'CH14', 'CH15', 'CH16'];

const MIXER_CURVE_FIELDS = [
    {
        key: 'deadzone',
        label: 'Deadzone',
        minimum: 0,
        maximum: 255,
    },
    {
        key: 'curve',
        label: 'Expo Curve',
        minimum: -127,
        maximum: 127,
    },
    {
        key: 'scale',
        label: 'Scale (%)',
        minimum: 100 + (SCALE_STEP_PERCENT * -128),
        maximum: 100 + (SCALE_STEP_PERCENT * 127),
    },
    {
        key: 'antideadzone',
        label: 'Antideadzone',
        minimum: 0,
        maximum: 255,
    },
    {
        key: 'offset',
        label: 'Offset',
        minimum: -127,
        maximum: 127,
    },
];

@customElement('custom-mixer-panel')
class CustomMixerPanel extends LitElement {
    mixerConfig = {};

    createRenderRoot() {
        this.loadMixerConfigFromState();
        return this;
    }

    render() {
        if (!this.isFeatureAvailable()) {
            return html`
                <div class="mui-panel mui--text-title">Custom Mixer</div>
                <div class="mui-panel">
                    <p>This firmware does not support this feature.</p>
                </div>
            `;
        }

        return html`
            <div class="mui-panel mui--text-title">Custom Mixer</div>
            <div class="mui-panel">
                <p>
                    Configure optional mixer logic for arcade tank drive outputs, two auxiliary curve channels,
                    and a custom arming switch channel.
                </p>
                <form class="mui-form">
                    ${this.renderArcadeTankDriveTable()}
                    ${this.renderAuxCurvesTable()}
                    ${this.renderArmingSwitchTable()}

                    <button class="mui-btn mui-btn--primary" @click="${this.save}">Save</button>
                </form>

                <div class="mui-divider" style="margin-top: 20px;"></div>
                <div style="margin-top: 16px;">
                    <h3>How to use Custom Mixer</h3>
                    <ul>
                        <li>
                            <b>Arcade tank drive:</b> assign throttle and steering input channels plus left and right
                            output channels. Use each curve row to shape behavior before and after the mixer sum.
                        </li>
                        <li>
                            <b>Aux curves:</b> assign a channel for AUX 1 and/or AUX 2 if you want a response
                            curve on those channels.
                        </li>
                        <li>
                            <b>Custom arming switch:</b> choose a channel and enable one or more switch positions
                            (Up/Mid/Down) that should count as armed. When not armed, the failsafe setting will be used for all channels.
                        </li>
                    </ul>
                    <h4>Curve Parameters</h4>
                    <ul>
                        <li>
                            <b>Deadzone:</b> Ignores small stick movements near the center so the output stays 0 still until you move it far enough.
                        </li>
                        <li>
                            <b>Scale (%):</b> Also known as gain or weight, a multiplier for the value as a percentage. 
                            Use a negative percentage to reverse the channel.
                        </li>
                        <li>
                            <b>Expo Curve:</b> Changes how sensitive the control feels, making it softer near the center and more responsive as you move further. 0 means no expo curve at all.
                        </li>
                        <li>
                            <b>Antideadzone:</b> Adds a small boost when you start moving so the signal overcomes other devices’ deadzones and responds immediately.
                        </li>
                        <li>
                            <b>Offset:</b> Simply shifts the output up or down.
                        </li>
                    </ul>
                </div>
            </div>
        `;
    }

    isFeatureAvailable() {
        const extraFeatureFlags = Number(elrsState.settings["extra-features-avail"]) || 0;
        return (extraFeatureFlags & (1 << EXTRA_FEATURE_CUSTOM_MIXER_BIT)) !== 0;
    }

    loadMixerConfigFromState() {
        const source = elrsState.config['custom-mixer'] || {};

        // Clone and normalize all expected fields so rendering and saving always use complete data.
        this.mixerConfig = this.sanitizeMixerConfig({
            ch_throttle: source.ch_throttle,
            ch_steering: source.ch_steering,
            ch_left: source.ch_left,
            ch_right: source.ch_right,
            curve_throttle: source.curve_throttle,
            curve_steering: source.curve_steering,
            curve_left: source.curve_left,
            curve_right: source.curve_right,
            ch_aux1: source.ch_aux1,
            curve_aux1: source.curve_aux1,
            ch_aux2: source.ch_aux2,
            curve_aux2: source.curve_aux2,
            ch_arm: source.ch_arm,
            arming_range: source.arming_range,
        });
    }

    renderArcadeTankDriveTable() {
        return html`
            <h3>Arcade tank drive mixer</h3>
            <table class="mui-table">
                <thead>
                    <tr>
                        <th>Setting</th>
                        <th>Throttle</th>
                        <th>Steering</th>
                        <th>Left</th>
                        <th>Right</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td>Channel</td>
                        <td>${this.renderChannelSelect('ch_throttle')}</td>
                        <td>${this.renderChannelSelect('ch_steering')}</td>
                        <td>${this.renderChannelSelect('ch_left')}</td>
                        <td>${this.renderChannelSelect('ch_right')}</td>
                    </tr>
                    ${MIXER_CURVE_FIELDS.map((field) => html`
                        <tr>
                            <td>${field.label}</td>
                            <td>${this.renderCurveInput('curve_throttle', field)}</td>
                            <td>${this.renderCurveInput('curve_steering', field)}</td>
                            <td>${this.renderCurveInput('curve_left', field)}</td>
                            <td>${this.renderCurveInput('curve_right', field)}</td>
                        </tr>
                    `)}
                </tbody>
            </table>
        `;
    }

    renderAuxCurvesTable() {
        return html`
            <h3 style="margin-top: 20px;">Aux curves</h3>
            <table class="mui-table">
                <thead>
                    <tr>
                        <th>Setting</th>
                        <th>AUX 1</th>
                        <th>AUX 2</th>
                    </tr>
                </thead>
                <tbody>
                    <tr>
                        <td>Channel</td>
                        <td>${this.renderChannelSelect('ch_aux1')}</td>
                        <td>${this.renderChannelSelect('ch_aux2')}</td>
                    </tr>
                    ${MIXER_CURVE_FIELDS.map((field) => html`
                        <tr>
                            <td>${field.label}</td>
                            <td>${this.renderCurveInput('curve_aux1', field)}</td>
                            <td>${this.renderCurveInput('curve_aux2', field)}</td>
                        </tr>
                    `)}
                </tbody>
            </table>
        `;
    }

    renderArmingSwitchTable() {
        return html`
            <h3 style="margin-top: 20px;">Custom arming switch</h3>
            <table class="mui-table">
                <tbody>
                    <tr>
                        <th style="width: 240px;">Channel</th>
                        <td>${this.renderChannelSelect('ch_arm')}</td>
                    </tr>
                    <tr>
                        <th><label for="arming-up">Armed when Up/High</label></th>
                        <td>
                            <div class="mui-checkbox">
                                <input id="arming-up" type="checkbox" ?checked="${this.getArmingRangeBit(2)}" @change="${(event) => this.updateArmingRangeBit(2, event.target.checked)}">
                            </div>
                        </td>
                    </tr>
                    <tr>
                        <th><label for="arming-mid">Armed when Mid</label></th>
                        <td>
                            <div class="mui-checkbox">
                                <input id="arming-mid" type="checkbox" ?checked="${this.getArmingRangeBit(1)}" @change="${(event) => this.updateArmingRangeBit(1, event.target.checked)}">
                            </div>
                        </td>
                    </tr>
                    <tr>
                        <th><label for="arming-down">Armed when Down/Low</label></th>
                        <td>
                            <div class="mui-checkbox">
                                <input id="arming-down" type="checkbox" ?checked="${this.getArmingRangeBit(0)}" @change="${(event) => this.updateArmingRangeBit(0, event.target.checked)}">
                            </div>
                        </td>
                    </tr>
                </tbody>
            </table>
        `;
    }

    renderChannelSelect(channelKey) {
        const currentValue = this.toUint8(this.mixerConfig[channelKey], 0, 16);
        return html`
            <div class="mui-select compact">
                <select @change="${(event) => this.updateChannel(channelKey, event.target.value)}">
                    ${DEFAULT_CHANNEL_OPTIONS.map((label, index) => html`
                        <option value="${index}" ?selected="${index === currentValue}">${label}</option>
                    `)}
                </select>
            </div>
        `;
    }

    renderCurveInput(curveKey, field) {
        const rawStoredValue = this.mixerConfig[curveKey][field.key];
        const displayValue = field.key === 'scale'
            ? this.scaleStorageToPercent(rawStoredValue)
            : rawStoredValue;

        return html`
            <div class="mui-textfield compact">
                <input
                    type="number"
                    min="${field.minimum}"
                    max="${field.maximum}"
                    .value="${String(displayValue)}"
                    @change="${(event) => this.updateCurveField(curveKey, field.key, event.target.value)}"
                >
            </div>
        `;
    }

    updateChannel(channelKey, value) {
        this.mixerConfig[channelKey] = this.toUint8(value, 0, 16);
        this.requestUpdate();
    }

    updateCurveField(curveKey, fieldKey, value) {
        if (!this.mixerConfig[curveKey]) {
            this.mixerConfig[curveKey] = this.defaultMixerCurve();
        }

        if (fieldKey === 'scale') {
            const clampedPercent = this.toInt(value, MIXER_CURVE_FIELDS[2].minimum, MIXER_CURVE_FIELDS[2].maximum, 100);
            this.mixerConfig[curveKey][fieldKey] = this.scalePercentToStorage(clampedPercent);
        } else if (fieldKey === 'deadzone' || fieldKey === 'antideadzone') {
            this.mixerConfig[curveKey][fieldKey] = this.toUint8(value, 0, 255);
        } else {
            this.mixerConfig[curveKey][fieldKey] = this.toInt8(value);
        }

        this.requestUpdate();
    }

    getArmingRangeBit(bitIndex) {
        const normalizedRange = this.toUint8(this.mixerConfig.arming_range, 0, 7);
        return (normalizedRange & (1 << bitIndex)) !== 0;
    }

    updateArmingRangeBit(bitIndex, enabled) {
        let normalizedRange = this.toUint8(this.mixerConfig.arming_range, 0, 7);
        if (enabled) {
            normalizedRange |= (1 << bitIndex);
        } else {
            normalizedRange &= ~(1 << bitIndex);
        }
        this.mixerConfig.arming_range = normalizedRange;
        this.requestUpdate();
    }

    save(event) {
        event.preventDefault();

        // Explicitly sanitize everything right before save so the payload is int8_t/uint8_t safe.
        const sanitizedMixer = this.sanitizeMixerConfig(this.mixerConfig);

        saveConfig({
            'custom-mixer': sanitizedMixer,
        }, () => {
            this.mixerConfig = sanitizedMixer;
            this.requestUpdate();
        });
    }

    sanitizeMixerConfig(inputConfig) {
        return {
            ch_throttle: this.toUint8(inputConfig.ch_throttle, 0, 16),
            ch_steering: this.toUint8(inputConfig.ch_steering, 0, 16),
            ch_left: this.toUint8(inputConfig.ch_left, 0, 16),
            ch_right: this.toUint8(inputConfig.ch_right, 0, 16),
            curve_throttle: this.sanitizeCurve(inputConfig.curve_throttle),
            curve_steering: this.sanitizeCurve(inputConfig.curve_steering),
            curve_left: this.sanitizeCurve(inputConfig.curve_left),
            curve_right: this.sanitizeCurve(inputConfig.curve_right),
            ch_aux1: this.toUint8(inputConfig.ch_aux1, 0, 16),
            curve_aux1: this.sanitizeCurve(inputConfig.curve_aux1),
            ch_aux2: this.toUint8(inputConfig.ch_aux2, 0, 16),
            curve_aux2: this.sanitizeCurve(inputConfig.curve_aux2),
            ch_arm: this.toUint8(inputConfig.ch_arm, 0, 16),
            arming_range: this.toUint8(inputConfig.arming_range, 0, 7),
        };
    }

    sanitizeCurve(inputCurve) {
        const source = inputCurve || {};
        return {
            deadzone: this.toUint8(source.deadzone, 0, 255),
            curve: this.toInt8(source.curve),
            scale: this.toInt8(source.scale),
            antideadzone: this.toUint8(source.antideadzone, 0, 255),
            offset: this.toInt8(source.offset),
        };
    }

    defaultMixerCurve() {
        return {
            deadzone: 0,
            curve: 0,
            scale: 0,
            antideadzone: 0,
            offset: 0,
        };
    }

    toUint8(value, minimum = 0, maximum = 255) {
        return this.toInt(value, minimum, maximum, 0);
    }

    toInt8(value) {
        return this.toInt(value, -128, 127, 0);
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

    scaleStorageToPercent(storedInt8Value) {
        return 100 + (SCALE_STEP_PERCENT * this.toInt8(storedInt8Value));
    }

    scalePercentToStorage(scalePercent) {
        const relativePercent = scalePercent - 100;
        const roundedSteps = Math.round(relativePercent / SCALE_STEP_PERCENT);
        return this.toInt8(roundedSteps);
    }
}

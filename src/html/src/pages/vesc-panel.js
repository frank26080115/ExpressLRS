import {html, LitElement} from "lit"
import {customElement, state} from "lit/decorators.js"
import FEATURES from "../../features.js"
import {elrsState, saveConfig} from "../utils/state.js"
import {PWM_MODE_SERIAL2RX} from "./connections-panel.js"

const DUTY_RANGE_SNAP_TO_MAX = 99500 // 99.5%
const POSITION_RANGE_SNAP_TO = 360000000 // 360 deg
const POSITION_RANGE_SNAP_MIN = POSITION_RANGE_SNAP_TO - 500000
const POSITION_RANGE_SNAP_MAX = POSITION_RANGE_SNAP_TO + 500000
// 1073741823 is the practical technical limit because the receiver keeps VESC
// range values below that to avoid signed int32 overflow in bidirectional math.
// We intentionally use 1000000000 instead so the configured ceiling is a clean
// round number instead of looking like random digits.
const VESC_RANGE_MAX = 1000000000
const EXTRA_FEATURE_VESC_BIT = 3
const EXTRA_FEATURE_VESC_TCP_BRIDGE_STARTED_BIT = 5
const PROTOCOL_VESC = 10
const PROTOCOL_SERIAL1_VESC = 12
const VESC_TELEM_CFG_POWER = 1 << 0
const VESC_TELEM_CFG_RPM = 1 << 1
const VESC_TELEM_CFG_TEMPERATURE = 1 << 2
const VESC_CFG_TCP_BRIDGE = 1 << 3
const VESC_CFG_EXTRAS_MASK = 0x0F

const COMMAND_OPTIONS = [
    {value: 0, label: "DISABLED"},
    {value: 5, label: "COMM_SET_DUTY"},
    {value: 6, label: "COMM_SET_CURRENT"},
    {value: 7, label: "COMM_SET_CURRENT_BRAKE"},
    {value: 8, label: "COMM_SET_RPM"},
    {value: 9, label: "COMM_SET_POS"},
    {value: 10, label: "COMM_SET_HANDBRAKE"},
]

const CHANNEL_OPTIONS = [
    {value: 0, label: "NONE / DISABLED"},
    {value: 1, label: "CH1"},
    {value: 2, label: "CH2"},
    {value: 3, label: "CH3"},
    {value: 4, label: "CH4"},
    {value: 5, label: "CH5"},
    {value: 6, label: "CH6"},
    {value: 7, label: "CH7"},
    {value: 8, label: "CH8"},
]

const clamp = (value, min, max) => Math.min(Math.max(value, min), max)

function defaultRow() {
    return {
        cmd: 0,
        channelX: 0,
        channelY: 0,
        bidirectional: false,
        range: 0,
    }
}

function decode_ufloat16(value) {
    const transport = clamp(Number(value) || 0, 0, 0xFFFF)
    const exponent = (transport >>> 11) & 0x1F
    const mantissa = transport & 0x7FF

    if (mantissa === 0) {
        return 0
    }

    const decoded = mantissa * (2 ** exponent)

    if (decoded > VESC_RANGE_MAX) {
        return VESC_RANGE_MAX
    }

    return decoded
}

function encode_ufloat16(value) {
    let mantissa = clamp(Math.trunc(Number(value) || 0), 0, VESC_RANGE_MAX)
    if (mantissa === 0) {
        return 0
    }

    let exponent = 0

    while (mantissa > 0x7FF) {
        mantissa = (mantissa + 1) >> 1
        exponent++
    }

    if (exponent > 0x1F) {
        exponent = 0x1F
        mantissa = 0x7FF
    }

    let r = ((exponent << 11) | (mantissa & 0x7FF)) >>> 0
    return r
}

function normalizeCommand(value) {
    const command = Number(value) || 0
    return COMMAND_OPTIONS.some((option) => option.value === command) ? command : 0
}

function decodeRow(rawValue) {
    const raw = Number(rawValue) >>> 0
    const cmd = normalizeCommand((raw >>> 1) & 0x7F)
    let range = decode_ufloat16((raw >>> 16) & 0xFFFF)
    if (cmd === 5 && range >= DUTY_RANGE_SNAP_TO_MAX) {
        range = 100000
    }
    else if (cmd === 9 && range >= POSITION_RANGE_SNAP_MIN && range <= POSITION_RANGE_SNAP_MAX) {
        range = POSITION_RANGE_SNAP_TO
    }
    return {
        cmd,
        channelX: clamp((raw >>> 8) & 0x0F, 0, 8),
        channelY: clamp((raw >>> 12) & 0x0F, 0, 8),
        bidirectional: (raw & 0x01) !== 0,
        range,
    }
}

function encodeRow(row) {
    const cmd = normalizeCommand(row.cmd)
    const channelX = clamp(Number(row.channelX) || 0, 0, 8)
    const channelY = clamp(Number(row.channelY) || 0, 0, 8)
    const bidirectional = row.bidirectional ? 1 : 0
    const rangeValue = clamp(Math.trunc(Number(row.range) || 0), 0, VESC_RANGE_MAX)
    const transportRange = encode_ufloat16(rangeValue)

    return (
        (((transportRange & 0xFFFF) << 16) |
            ((channelY & 0x0F) << 12) |
            ((channelX & 0x0F) << 8) |
            ((cmd & 0x7F) << 1) |
            bidirectional) >>> 0
    )
}

function getStoredConfig() {
    const stored = Array.isArray(elrsState.config["vesc-cfg"]) ? elrsState.config["vesc-cfg"] : []
    const output = new Array(6).fill(0)
    for (let i = 0; i < 6; i++) {
        output[i] = Number(stored[i] || 0) >>> 0
    }
    return output
}

function getStoredExtras() {
    return Number(elrsState.config["vesc-cfg-extras"] || 0) & VESC_CFG_EXTRAS_MASK
}

@customElement("vesc-panel")
class VescPanel extends LitElement {
    @state() accessor rows = []
    @state() accessor extras = 0

    constructor() {
        super()
        this.rows = getStoredConfig().map((item) => decodeRow(item))
        this.extras = getStoredExtras()
        this._save = this._save.bind(this)
    }

    createRenderRoot() {
        return this
    }

    render() {
        if (!this._isFeatureAvailable()) {
            return html`
                <div class="mui-panel mui--text-title">VESC</div>
                <div class="mui-panel">
                    <p>This firmware does not support this feature.</p>
                </div>
            `
        }

        return html`
            <style>
                .vesc-table {
                    width: 100%;
                }

                .vesc-table th,
                .vesc-table td {
                    vertical-align: middle;
                }

                .vesc-inline-input {
                    width: 100%;
                    min-width: 8rem;
                }

                .vesc-inline-select {
                    width: 100%;
                    min-width: 10rem;
                }

                .vesc-channel-cell {
                    min-width: 18rem;
                }

                .vesc-channel-pair {
                    align-items: center;
                    flex-wrap: nowrap;
                }

                .vesc-channel-pair .mui-select {
                    flex: 1 1 0;
                    margin-bottom: 0;
                }

                .vesc-range-cell {
                    min-width: 10rem;
                }

                .vesc-muted {
                    color: #7a7a7a;
                }

                .vesc-group-note {
                    margin: 0 0 1rem;
                }

                .vesc-warning {
                    margin-bottom: 1rem;
                }
            </style>
            <div class="mui-panel mui--text-title">VESC</div>
            <div class="mui-panel">
                <p>Configure up to three VESC mappings per serial port.</p>

                ${this._renderGroup({
                    title: "Serial 1",
                    startIndex: 0,
                    enabled: this._isSerial1Enabled(),
                    available: true,
                    message: "Use the Serial panel to set Serial 1 Protocol to VESC before these mappings will be active.",
                })}

                ${this._hasSerial2() ? this._renderGroup({
                    title: "Serial 2",
                    startIndex: 3,
                    enabled: this._isSerial2Enabled(),
                    available: true,
                    message: "Use the Serial panel to set Serial 2 Protocol to VESC before these mappings will be active.",
                }) : ""}

                <fieldset>
                    <legend>Extras</legend>
                    ${this._renderTelemetryWarning()}
                    <div class="mui-checkbox">
                        <input
                            id="vesc-extra-power"
                            type="checkbox"
                            ?checked="${(this.extras & VESC_TELEM_CFG_POWER) !== 0}"
                            @change="${(e) => this._setExtraFlag(VESC_TELEM_CFG_POWER, e.target.checked)}"
                        />
                        <label for="vesc-extra-power">Enable Power Telemetry</label>
                    </div>
                    <div class="mui-checkbox">
                        <input
                            id="vesc-extra-rpm"
                            type="checkbox"
                            ?checked="${(this.extras & VESC_TELEM_CFG_RPM) !== 0}"
                            @change="${(e) => this._setExtraFlag(VESC_TELEM_CFG_RPM, e.target.checked)}"
                        />
                        <label for="vesc-extra-rpm">Enable RPM Telemetry</label>
                    </div>
                    <div class="mui-checkbox">
                        <input
                            id="vesc-extra-temperature"
                            type="checkbox"
                            ?checked="${(this.extras & VESC_TELEM_CFG_TEMPERATURE) !== 0}"
                            @change="${(e) => this._setExtraFlag(VESC_TELEM_CFG_TEMPERATURE, e.target.checked)}"
                        />
                        <label for="vesc-extra-temperature">Enable Temperature Telemetry</label>
                    </div>
                    <div class="mui-checkbox">
                        <input
                            id="vesc-extra-tcp"
                            type="checkbox"
                            ?checked="${(this.extras & VESC_CFG_TCP_BRIDGE) !== 0}"
                            @change="${(e) => this._setExtraFlag(VESC_CFG_TCP_BRIDGE, e.target.checked)}"
                        />
                        <label for="vesc-extra-tcp">Enable TCP Bridge</label>
                    </div>
                    <p class="vesc-group-note vesc-muted">
                        TCP bridge port: <code>65102</code> ${this._isTcpBridgeStarted() ? "✅" : "❌"}
                    </p>
                </fieldset>

                <button
                    class="mui-btn mui-btn--small mui-btn--primary"
                    ?disabled="${!this.checkChanged()}"
                    @click="${this._save}"
                >
                    Save
                </button>

                <div class="mui-divider" style="margin: 1.5rem 0;"></div>
                <div class="mui--text-title">Helpful Hints</div>
                <div class="vesc-muted">
                    <p>For duty cycle, a range of <code>100000</code> represents 100%.</p>
                    <p>For current and current brake, the range unit is milliamps.</p>
                    <p>For position, the value is degrees with VESC position scaling applied.</p>
                    <p>For position with one channel, the selected channel is mapped directly across the configured range. If you also set the second channel, the two channels are treated like X and Y stick inputs and converted into an angle around the circle.</p>
                    <p>For RPM, the value is raw RPM.</p>
                </div>
            </div>
        `
    }

    _isFeatureAvailable() {
        const extraFeatureFlags = Number(elrsState.settings["extra-features-avail"]) || 0
        return (extraFeatureFlags & (1 << EXTRA_FEATURE_VESC_BIT)) !== 0
    }

    _isTcpBridgeStarted() {
        const extraFeatureFlags = Number(elrsState.settings["extra-features-avail"]) || 0
        return (extraFeatureFlags & (1 << EXTRA_FEATURE_VESC_TCP_BRIDGE_STARTED_BIT)) !== 0
    }

    _renderGroup({title, startIndex, enabled, message}) {
        return html`
            <fieldset ?disabled="${!enabled}">
                <legend>${title}</legend>
                ${!enabled ? html`<p class="vesc-group-note vesc-muted">${message}</p>` : ""}
                <table class="mui-table vesc-table">
                    <thead>
                    <tr>
                        <th>Command</th>
                        <th>Channel</th>
                        <th class="mui--text-center" style="padding-right: 0.75rem;">Bi-dir</th>
                        <th>Range</th>
                    </tr>
                    </thead>
                    <tbody>
                    ${[0, 1, 2].map((offset) => this._renderRow(startIndex + offset))}
                    </tbody>
                </table>
            </fieldset>
        `
    }

    _renderRow(index) {
        const row = this.rows[index] || defaultRow()
        const showYChannel = row.cmd === 9
        return html`
            <tr>
                <td>${this._renderSelect(COMMAND_OPTIONS, row.cmd, (value) => this._updateRow(index, {cmd: value}))}</td>
                <td class="vesc-channel-cell vesc-channel-pair">
                    ${this._renderSelect(CHANNEL_OPTIONS, row.channelX, (value) => this._updateRow(index, {channelX: value}))}
                    ${showYChannel
                        ? this._renderSelect(CHANNEL_OPTIONS, row.channelY, (value) => this._updateRow(index, {channelY: value}))
                        : ""}
                </td>
                <td class="mui--text-center">
                    <div class="mui-checkbox">
                        <input
                            id="vesc-bidir-${index}"
                            type="checkbox"
                            ?checked="${row.bidirectional}"
                            @change="${(e) => this._updateRow(index, {bidirectional: e.target.checked})}"
                        />
                        <label for="vesc-bidir-${index}"></label>
                    </div>
                </td>
                <td class="vesc-range-cell">
                    <div class="mui-textfield">
                        <input
                            class="vesc-inline-input"
                            type="number"
                            min="0"
                            max="${VESC_RANGE_MAX}"
                            .value="${String(row.range)}"
                            @input="${(e) => this._updateRow(index, {range: clamp(Number(e.target.value || 0), 0, VESC_RANGE_MAX)})}"
                        />
                    </div>
                </td>
            </tr>
        `
    }

    _renderSelect(options, selectedValue, onChange) {
        return html`
            <div class="mui-select">
                <select class="vesc-inline-select" @change="${(e) => onChange(parseInt(e.target.value))}">
                    ${options.map((option) => html`
                        <option .value="${String(option.value)}" ?selected="${option.value === selectedValue}">${option.label}</option>
                    `)}
                </select>
            </div>
        `
    }

    _updateRow(index, patch) {
        const nextRows = this.rows.map((row) => ({...row}))
        const current = nextRows[index] || defaultRow()
        const updated = {...current, ...patch}
        if (patch.cmd === 5 && current.cmd !== 5) {
            updated.range = 100000
        }
        if (updated.cmd !== 9) {
            updated.channelY = 0
        }
        nextRows[index] = updated
        this.rows = nextRows
    }

    _isSerial1Enabled() {
        return elrsState.config["serial-protocol"] === PROTOCOL_VESC
    }

    _hasSerial2() {
        return !FEATURES.IS_8285 && elrsState.config["serial1-protocol"] !== undefined
    }

    _isSerial2Enabled() {
        return this._hasSerial2() && elrsState.config["serial1-protocol"] === PROTOCOL_SERIAL1_VESC
    }

    _renderTelemetryWarning() {
        const message = this._getTelemetryWarningMessage()
        if (!message) {
            return ""
        }

        return html`
            <div class="warning-bg vesc-warning">
                ${message}
            </div>
        `
    }

    _getTelemetryWarningMessage() {
        if (FEATURES.IS_8285) {
            return this._hasMainSerialRxPin()
                ? ""
                : "Telemetry requires serial_rx."
        }

        return (this._hasMainSerialRxPin() || this._hasSecondarySerialRxPin())
            ? ""
            : "Telemetry requires at least one serial RX pin to be defined."
    }

    _hasMainSerialRxPin() {
        return !!elrsState.settings.has_serial_pins
    }

    _hasSecondarySerialRxPin() {
        return false
    }

    _encodeRows() {
        const source = this.rows.length === 6 ? this.rows : getStoredConfig().map((item) => decodeRow(item))
        return source.map((row) => encodeRow(row))
    }

    _setExtraFlag(flag, enabled) {
        if (enabled) {
            this.extras = (this.extras | flag) & VESC_CFG_EXTRAS_MASK
        }
        else {
            this.extras = this.extras & ~flag
        }
    }

    _save(e) {
        e.preventDefault()
        saveConfig({
            "vesc-cfg": this._encodeRows(),
            "vesc-cfg-extras": this.extras & VESC_CFG_EXTRAS_MASK,
        }, () => {
            this.requestUpdate()
        })
    }

    checkChanged() {
        const current = getStoredConfig()
        const next = this._encodeRows()
        for (let i = 0; i < 6; i++) {
            if ((current[i] >>> 0) !== (next[i] >>> 0)) {
                return true
            }
        }
        return getStoredExtras() !== (this.extras & VESC_CFG_EXTRAS_MASK)
    }
}

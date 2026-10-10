// Minimaler Ersatz für @companion-module/base, damit das Modul ohne Companion testbar ist.
const InstanceStatus = {
	Ok: 'ok',
	Connecting: 'connecting',
	Disconnected: 'disconnected',
	ConnectionFailure: 'connection_failure',
	BadConfig: 'bad_config',
	UnknownError: 'unknown_error',
	UnknownWarning: 'unknown_warning',
}

class InstanceBase {
	constructor(internal) {
		this.internal = internal
		this.label = 'showcom'
		this.calls = { status: [], logs: [], checks: 0 }
		this.variables = {}
		this.variableDefs = []
		this.actions = {}
		this.feedbacks = {}
		this.presets = {}
	}
	updateStatus(status, message) {
		this.calls.status.push([status, message])
		this.currentStatus = status
		this.currentMessage = message
	}
	log(level, msg) {
		this.calls.logs.push([level, msg])
	}
	setActionDefinitions(a) {
		this.actions = a
	}
	setFeedbackDefinitions(f) {
		this.feedbacks = f
	}
	setVariableDefinitions(v) {
		this.variableDefs = v
	}
	setVariableValues(v) {
		Object.assign(this.variables, v)
	}
	setPresetDefinitions(p) {
		this.presets = p
	}
	checkFeedbacks() {
		this.calls.checks++
	}
}

module.exports = {
	InstanceBase,
	InstanceStatus,
	runEntrypoint: () => {},
	combineRgb: (r, g, b) => ((r & 255) << 16) | ((g & 255) << 8) | (b & 255),
	Regex: { IP: '/.*/', PORT: '/.*/' },
}

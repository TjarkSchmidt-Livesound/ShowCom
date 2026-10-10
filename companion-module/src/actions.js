const { triggerChoices, firstId } = require('./choices')

function buildActions(self) {
	const state = self.state
	const triggers = triggerChoices(state, true)

	return {
		ack_alarm: {
			name: 'Alarm quittieren',
			description: 'Quittiert den Namens-Alarm in ShowCom (wie ein Klick auf das Banner)',
			options: [
				{
					type: 'checkbox',
					id: 'resetTriggers',
					label: 'Auch alle Trigger in Companion zurücksetzen',
					default: true,
				},
			],
			callback: async (action) => {
				if (action.options.resetTriggers && state.reset('*')) self.publish(true)
				await self.sendCommand('ack')
			},
		},
		listen: {
			name: 'Zuhören an/aus',
			options: [
				{
					type: 'dropdown',
					id: 'mode',
					label: 'Modus',
					default: 'toggle',
					choices: [
						{ id: 'toggle', label: 'Umschalten' },
						{ id: 'on', label: 'An' },
						{ id: 'off', label: 'Aus (Pause)' },
					],
				},
			],
			callback: async (action) => {
				const cmd = { on: 'listen', off: 'pause' }[action.options.mode] || 'toggleListen'
				await self.sendCommand(cmd)
			},
		},
		popup: {
			name: 'Alarm-Popup ein/aus (am ShowCom-Rechner)',
			options: [],
			callback: async () => self.sendCommand('popup'),
		},
		test_alarm: {
			name: 'Testalarm in ShowCom auslösen',
			options: [],
			callback: async () => self.sendCommand('testAlarm'),
		},
		reset_trigger: {
			name: 'Trigger zurücksetzen',
			options: [
				{ type: 'dropdown', id: 'trigger', label: 'Trigger', default: '*', choices: triggers },
				{ type: 'checkbox', id: 'counts', label: 'Auch Zähler und letzten Text löschen', default: false },
			],
			callback: async (action) => {
				if (state.reset(action.options.trigger || '*', !!action.options.counts)) self.publish(true)
				else self.publish(false)
			},
		},
		simulate_trigger: {
			name: 'Trigger testweise auslösen (nur Companion)',
			options: [{ type: 'dropdown', id: 'trigger', label: 'Trigger', default: firstId(triggers.slice(1)), choices: triggers.slice(1) }],
			callback: async (action) => {
				if (state.simulate(action.options.trigger, Date.now())) self.publish(true)
			},
		},
	}
}

module.exports = { buildActions }

'use strict';

// Match the scopes used by the built-in GitHub extension so it can reuse the session.
const GITHUB_SCOPES = Object.freeze(['repo', 'workflow', 'user:email', 'read:user']);

function isCancellation(error) {
	const message = typeof error === 'string' ? error : error?.message;
	return error?.name === 'CancellationError'
		|| ['Canceled', 'Cancelled', 'User Cancelled', 'User canceled.'].includes(message);
}

function createGitHubAccounts(api) {
	const item = api.window.createStatusBarItem('jbro.github.account', api.StatusBarAlignment.Right, 200);
	item.name = api.l10n.t('GitHub account');
	let account;
	let login;
	let signingIn = false;
	let disposed = false;
	let revision = 0;
	let unavailable = false;

	function render() {
		if (disposed) {
			return;
		}
		if (signingIn) {
			item.text = '$(loading~spin) ' + api.l10n.t('Signing in to GitHub...');
			item.tooltip = api.l10n.t('Complete sign-in in the GitHub browser page.');
			item.command = 'jbro.git.signIn';
		} else if (account) {
			item.text = '$(github) ' + account.label;
			item.tooltip = api.l10n.t('GitHub: {0}. Click to manage accounts or sign out.', account.label);
			item.command = 'jbro.git.manageAccount';
		} else {
			item.text = '$(github) ' + api.l10n.t('Sign in to GitHub');
			item.tooltip = unavailable
				? api.l10n.t('GitHub account could not be read. Click to try signing in.')
				: api.l10n.t('Sign in to use GitHub with JBro Script Editor.');
			item.command = 'jbro.git.signIn';
		}
		item.show();
		void api.commands.executeCommand('setContext', 'jbro.github.signedIn', Boolean(account));
	}

	function takeAccount(session) {
		// Never retain, return, log, or persist the access token. The provider owns credentials.
		account = session ? { id: session.account.id, label: session.account.label } : undefined;
		unavailable = false;
		render();
	}

	async function refresh() {
		if (disposed || signingIn) {
			return;
		}
		const current = ++revision;
		try {
			const session = await api.authentication.getSession('github', GITHUB_SCOPES, { silent: true });
			if (!disposed && current === revision) {
				takeAccount(session);
			}
		} catch {
			if (!disposed && current === revision) {
				unavailable = true;
				render();
			}
		}
	}

	function signIn() {
		if (disposed) {
			return Promise.resolve(false);
		}
		if (login) {
			return login;
		}
		++revision;
		signingIn = true;
		render();
		login = (async () => {
			try {
				const session = await api.authentication.getSession('github', GITHUB_SCOPES, { createIfNone: true });
				if (!disposed) {
					takeAccount(session);
				}
				return Boolean(session);
			} catch (error) {
				if (!disposed && !isCancellation(error)) {
					void api.window.showErrorMessage(api.l10n.t('GitHub sign-in failed. Try again.'));
				}
				return false;
			} finally {
				login = undefined;
				signingIn = false;
				render();
			}
		})();
		return login;
	}

	const changed = api.authentication.onDidChangeSessions(event => {
		if (event.provider.id === 'github') {
			return refresh();
		}
	});
	// Display the entry before querying; activation must never start interactive sign-in.
	render();
	return {
		refresh,
		signIn,
		manageAccount: () => disposed ? Promise.resolve() : api.commands.executeCommand('workbench.action.manageAccounts'),
		dispose() {
			disposed = true;
			++revision;
			changed.dispose();
			item.dispose();
		}
	};
}

module.exports = { createGitHubAccounts, GITHUB_SCOPES };

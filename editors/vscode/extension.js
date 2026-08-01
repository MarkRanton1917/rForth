'use strict';

const vscode = require('vscode');
const { analyze } = require('./parser');

const legend = new vscode.SemanticTokensLegend(['parameter'], []);

function activate(context) {
  context.subscriptions.push(
    vscode.languages.registerDocumentSemanticTokensProvider(
      { language: 'rforth' },
      {
        provideDocumentSemanticTokens(document) {
          const builder = new vscode.SemanticTokensBuilder(legend);
          for (const token of analyze(document.getText())) {
            const pos = document.positionAt(token.start);
            builder.push(pos.line, pos.character, token.length,
                         legend.tokenTypes.indexOf(token.type));
          }
          return builder.build();
        },
      },
      legend));
}

function deactivate() {}

module.exports = { activate, deactivate };

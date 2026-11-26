import {
  JupyterFrontEnd,
  JupyterFrontEndPlugin
} from '@jupyterlab/application';
import { ToolbarButton } from '@jupyterlab/apputils';
import { DocumentRegistry } from '@jupyterlab/docregistry';
import {
  INotebookModel,
  NotebookPanel
} from '@jupyterlab/notebook';
import { CodeCell } from '@jupyterlab/cells';
import { IDisposable, DisposableDelegate } from '@lumino/disposable';

class VisualizeMemoryButton
  implements DocumentRegistry.IWidgetExtension<NotebookPanel, INotebookModel>
{
  createNew(
    panel: NotebookPanel,
    _context: DocumentRegistry.IContext<INotebookModel>
  ): IDisposable {
    const button = new ToolbarButton({
      className: 'cpp-visualize-btn',
      label: '▶ Визуализирай паметта',
      tooltip: 'Инструментира C++ кода и показва паметта стъпка по стъпка',
      onClick: async () => {
        const activeCell = panel.content.activeCell;
        if (!activeCell || !(activeCell instanceof CodeCell)) {
          return;
        }

        const studentCode = activeCell.model.sharedModel.getSource();
        const sessionContext = panel.sessionContext;
        const kernel = sessionContext.session?.kernel;

        if (!kernel) {
          alert('C++ ядрото все още зарежда. Изчакай няколко секунди.');
          return;
        }

        button.node.style.opacity = '0.6';
        try {
          const resp = await fetch('http://127.0.0.1:8001/instrument', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ code: studentCode })
          });

          const data = await resp.json();
          if (!data.instrumented_code) {
            console.error('Грешка при инструментиране:', data.stderr);
            return;
          }

          activeCell.outputArea.model.clear();

          const future = kernel.requestExecute({
            code: data.instrumented_code,
            silent: false,
            store_history: false
          });

          future.onIOPub = (msg) => {
            const msgType = msg.header.msg_type;
            if (
              msgType === 'display_data' ||
              msgType === 'execute_result' ||
              msgType === 'stream' ||
              msgType === 'error'
            ) {
              activeCell.outputArea.model.add({
                output_type: msgType,
                ...(msg.content as any)
              });
            }
          };

          await future.done;
        } catch (err) {
          console.error('Грешка при връзка с instrumenter сървъра:', err);
          alert('Неуспешна връзка с инструмента за визуализация (порт 8001).');
        } finally {
          button.node.style.opacity = '1';
        }
      }
    });

    button.node.style.backgroundColor = '#0d9488';
    button.node.style.color = '#ffffff';
    button.node.style.borderRadius = '4px';
    button.node.style.padding = '0 10px';
    button.node.style.marginLeft = '8px';

    panel.toolbar.insertItem(10, 'visualizeMemory', button);

    return new DisposableDelegate(() => {
      button.dispose();
    });
  }
}

const plugin: JupyterFrontEndPlugin<void> = {
  id: 'cpp-visualizer-ext:plugin',
  autoStart: true,
  activate: (app: JupyterFrontEnd) => {
    app.docRegistry.addWidgetExtension('Notebook', new VisualizeMemoryButton());
  }
};

export default plugin;

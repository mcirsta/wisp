---
trigger: always_on
---

## Log Capture Instructions
When you need to capture logs for debugging Wisp (e.g., for layout, network, or rendering issues), use the following command (using the appropriate frontend for your platform, e.g., `nsqt` on Linux):

```bash
./frontends/qt/wisp-qt -split-logs <URL>
```

### Important Notes:
- **GUI Application**: `nsqt` is the project's actual GUI browser application. 
- **Distinguish from Built-in Tools**: Do not confuse the project's GUI applications (like `nsqt`) with the built-in Antigravity browser tools (like `browser_subagent`). 
- **Usage**: Use the project's GUI application when you need to observe the behavior of the Wisp engine specifically. The built-in Antigravity browser is for general web research and external documentation.
- **Output**: Running this command creates a `wisp-logs` folder.
- **Log Levels**: Logs are split into files by level. Lower-level files include all higher-level logs.
  - `ns-deepdebug.txt` is the lowest level and contains **all** logs from all levels. Use this for a complete picture.
- **Process Termination**: After capturing logs and identifying the necessary information, you **must** kill the project's GUI application process before proceeding with further tasks.
- **Data Retention**: Do not delete any debugging logs (e.g., `neosurf-logs` folder), other tools/scripts used for debugging, or any logging statements added to the code until after the user confirms that the problem was fixed or specifically instructs you to do so.
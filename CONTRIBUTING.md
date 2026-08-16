# Contributing to OpenGauge

Thank you for improving OpenGauge.

## Before you start

- Discuss large changes in an issue first.
- Keep one pull request focused on one problem.
- Do not commit build output from `.pio`.
- Keep the existing source style where practical.
- Use clear names and short comments. Explain why a change is needed.

## Test your change

Run the checks that match your change:

```sh
pio run
```

For display changes, generate a matching preview with
`tools/gauge_preview.py`. Include an updated PNG when the visual result
changes.

## Hardware contributions

PCB files, wiring drawings, and test results are welcome after the hardware
release process is defined. Include board revision, tool version, and clear
photographs or renders where possible. Do not publish vehicle-specific wiring
instructions without safety notes.

## Pull requests

Describe the problem, the change, and how you tested it. State any hardware,
vehicle, or sensor assumptions. Update the relevant documentation when the
user-visible behaviour changes.

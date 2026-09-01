# Biosignal application architecture

## Boundaries

The C++ core owns transport integration, device sessions, decoded sample
frames, DSP-independent recording and playback.  It has no Qt dependency.
Qt adapters expose DTOs/signals and run polling on a worker thread.  Pages are
views backed by page-specific ViewModels and never access another page's state.
`AppContext` owns the single active `DeviceSession` adapter, the `DataHub`,
recording services and the AI worker adapter. There is no multi-device
coordinator: discovery is stateless and implemented by `DeviceSession`'s
port-scan/probe helpers, with `QtDeviceDiscoveryAdapter` running those calls
off the GUI thread. Once a port is selected, `AppContext::setSession()`
replaces the one active session.

Discovery probes only `/dev/ttyUSB*` and `/dev/ttyACM*`, keeps each port open
for up to three requests, waits 650 ms per request, and flushes stale RX bytes
between attempts. Probe diagnostics are emitted to the Qt log; a port is
listed only after a CRC-valid response with a valid channel count.

`DataHub` is the fan-out point.  A single decoded stream can feed monitoring,
raw recording, dataset capture and inference.  The bounded session buffer keeps
receiving MCU data even if a consumer or Python process stops.

The ADS1298 device profile is kept in the protocol/session layer: eight
physical channels, 1000 Hz initial rate, 24-byte initial push packets, fixed
internal 2.4 V reference, and validated PGA/MUX capabilities.  Qt pages consume
these capabilities rather than duplicating wire-level assumptions.

Only one acquisition mode owns the device at a time: Monitoring, Dataset
Capture, Inference or Playback.  Device settings remain global across mode
changes; ownership transitions stop the previous stream before starting the
next one.

## Pages

Devices contains discovery, global ADC settings and hardware diagnostics.
Monitoring contains channel-local display selection, filtering, FFT, playback
and a rolling DC-removed RMS noise metric.  AI is a navigation group with Data
Collection, Training and Inference pages.

## File formats

Raw sessions use the new BSIG v2 binary container.  It stores unfiltered values,
physical channel ids, sample rate, push/configuration metadata and a finalized
sample count; playback uses the same sample-source abstraction as live data.

Datasets are `*.bset` directories containing a JSON manifest, binary chunks
and an `examples.jsonl` index.  A task declares its type, physical channels,
window/stride, labels and preprocessing.  A model bundle (`*.aimodel`) carries
the matching schema, labels, normalization and weights metadata.

## AI worker

Python is started on demand with `QProcess`; monitoring and recording do not
require Python.  The worker protocol is versioned JSON messages.  Dataset
training exchanges bundle paths, while inference exchanges bounded windows and
returns sorted top-5 probabilities.  A crash stops only training/inference,
reports the error, and leaves acquisition and the circular buffer alive.

The checked-in Python worker is deliberately a placeholder.  Replace its model
adapter and trainer with PyTorch code without changing the C++ page contract.

Datasets now use manifest schema v2. The manifest embeds a task descriptor
(`task_id`, `task_type`, `model_family`, channels, window/stride,
preprocessing/normalization), labels and training defaults. Data Collection
stores one or more labels and repetitions as binary windows plus an
`examples.jsonl` index. Training creates an automatically versioned
`model-vNNN.aimodel` directory; its manifest records the source dataset and
the geometry required for compatibility checks.

The Python worker speaks protocol-versioned JSON lines. Model code is selected
through `ai_worker/models/<model_family>.py`, so a project-specific PyTorch
implementation can be added without changing Qt pages. The default `custom`
backend is deterministic and exists only as an integration smoke-test backend.

The main window navigation is a `QTreeWidget` inside a horizontal
`QSplitter`. The AI group is collapsible, the pane is mouse-resizable between
180 and 420 pixels, and splitter/expanded state are persisted with
`QSettings`. All application-facing strings are English.

## Alternatives considered

- Embedded CPython would couple the GUI lifetime to Python ABI and virtualenv
  management; a worker gives process isolation and independent upgrades.
- TorchScript/ONNX in C++ is useful for deployment, but libtorch/runtime
  packaging is deferred until a real model exists.
- CSV/JSONL samples are easy to inspect but too large for high-rate streams;
  binary chunks plus a JSON manifest keep both streaming and Python parsing
  practical.
- A Qt-only MVC design would leave acquisition state scattered through widgets;
  QObject ViewModels make state transitions testable while keeping QWidget
  code focused on presentation.

Protocol framing and command semantics are defined by the files under
`Protocols/`; this document only describes where that integration belongs.

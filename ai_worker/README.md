# AI worker scaffold

The worker is intentionally model-agnostic and speaks protocol-versioned
JSON-lines messages. Add a model family in `models/<family>.py` implementing
`train(dataset, output, report, config)`, `load(bundle)` and
`predict(model, values, rows, channels)`. Bundle metadata must describe the
task id/type, physical channel ids, sample rate, window/stride, labels and
preprocessing so the C++ UI can validate inputs. The checked-in `custom` family
only verifies process/IPC integration and must not be used for scientific
results.

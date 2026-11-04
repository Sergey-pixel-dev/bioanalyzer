# AI worker

The worker is model-agnostic and speaks protocol-versioned JSON-lines
messages (protocol 3). Add a model module in `models/<name>.py` implementing
`meta()`, `train(dataset, output, report, config)`, `load(bundle)` and
`predict(model, values, rows, channels)`.

`meta()` describes the model: the task type it solves (`task_type`, written
into the model manifest and shown on the recognition page), the kind of targets
it learns from (`target_type`: a dataset suits the model if its trials have
targets of this kind and the signal fits, whatever task description the dataset
was collected by), its hyperparameters (`int`, `float`, `enum`, `bool`, `str`; a float with
`"format": "exp"` is shown as `1e-3`; an int with `"special"` at its minimum
means "random" and the worker picks a value) and what it returns in
`outputs`:

```json
"outputs": {
  "training":  [{"name": "train_loss", "axis": "left"}, {"name": "val_loss", "axis": "left"},
                {"name": "val_accuracy", "axis": "right"}, {"name": "learning_rate"}],
  "inference": {"kind": "top_k", "max_k": 5}
}
```

`train()` calls `report(epoch, epochs, best=..., **values)` after every
epoch with the values declared in `outputs.training`; the worker sends them as
`{"type": "metric", "epoch", "epochs", "values", "best"}` and keeps the
training history in `history.json` of the model folder: the hyperparameters
with the seed actually used, the declared outputs, the planned number of
epochs and the values by epoch. A resumed training (`config["resume"]` is the
checkpoint) keeps the earlier epochs; its first report drops the epochs from
the reported one on. `predict()` returns a result of the kind declared in
`outputs.inference`; the application shows only `top_k` (labels with
probabilities) and refuses a model of another kind when it is chosen.

A module whose model takes a segment shorter than a trial declares an `int`
hyperparameter `window_ms`: the application then writes the model window as
`window_ms * sample_rate // 1000` samples into the bundle manifest and uses a
quarter of it as the default inference stride. Trials marked
`"excluded": true` in `examples.jsonl` (the Datasets page) must be skipped.

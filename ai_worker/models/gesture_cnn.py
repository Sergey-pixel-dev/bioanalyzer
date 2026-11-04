"""Multi-scale 1D Hydra-CNN for gesture classification from surface EMG.

The network follows HAMSNet (Sobahi et al., Symmetry 2026, 18, 777) without
its multi-head self-attention and squeeze-and-excitation blocks: in the
authors' ablation the Hydra block alone was the most accurate. The dataset
is kept raw; this module owns the preprocessing (band-pass, mains notch,
scaling by training-set statistics) and the cutting of trials into short
windows, so training and inference use exactly the same numeric domain.
"""

import json
import math
import os
import random

import numpy as np
from scipy.signal import butter, iirnotch, sosfiltfilt, tf2sos
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset

# Version of the network and of the preprocessing stored with the weights;
# weights of another version are not loaded.
ARCH = 3
# Version of the dataset format this module reads.
DATASET_SCHEMA = 5


def meta():
    return {
        "name": "Gesture Hydra 1D-CNN",
        "task_type": "classification",
        "target_type": "label",
        # What the model returns: after every training epoch the values in
        # "training" (an axis puts a value on the chart by default), and on
        # every window at recognition the most probable labels.
        "outputs": {
            "training": [
                {"name": "train_loss", "axis": "left"},
                {"name": "val_loss", "axis": "left"},
                {"name": "val_accuracy", "axis": "right"},
                {"name": "learning_rate"},
            ],
            "inference": {"kind": "top_k", "max_k": 5},
        },
        # Weights file with the best epoch; last.pt holds the last epoch with
        # the optimizer state, from which training can be resumed.
        "weights": "weights.pt",
        "resume": True,
        # Defaults after the paper: AdamW with weight decay, the learning rate
        # falling along a cosine, batches of 256, 200 epochs, windows of 250 ms
        # taken every 62 ms.
        "hyperparameters": [
            {"name": "epochs", "type": "int", "min": 1, "max": 10000, "default": 200},
            {"name": "batch_size", "type": "int", "min": 1, "max": 4096, "default": 256},
            # Small rates are shown and entered as 1e-3.
            {"name": "learning_rate", "type": "float", "min": 1e-7, "max": 1.0, "default": 1e-3, "format": "exp"},
            {"name": "weight_decay", "type": "float", "min": 0.0, "max": 1.0, "default": 1e-4, "format": "exp"},
            {"name": "validation_split", "type": "float", "min": 0.0, "max": 0.9, "default": 0.2},
            # The model input is a window of window_ms; the windows are cut from
            # a trial every hop_ms after skip_ms from the cue: the subject's
            # reaction and the build-up of force are not the gesture yet.
            {"name": "window_ms", "type": "int", "min": 50, "max": 10000, "default": 250},
            {"name": "hop_ms", "type": "int", "min": 1, "max": 10000, "default": 62},
            {"name": "skip_ms", "type": "int", "min": 0, "max": 5000, "default": 500},
            {"name": "notch", "type": "enum", "choices": ["50", "60", "off"], "default": "50"},
            # -1 means "random": the worker picks a seed and writes it into the
            # training history, so that a run can be repeated on the same split.
            {"name": "seed", "type": "int", "min": -1, "max": 2147483647, "default": -1, "special": "random"},
            {"name": "device", "type": "enum", "choices": ["cpu", "cuda"],
             "default": "cuda" if torch.cuda.is_available() else "cpu"},
        ],
    }


class HydraCNN(nn.Module):
    """Stem, Hydra block, 1 x 1 fusion, global average pooling and a linear layer.

    The stem has two convolutions (kernels 15 and 9, 32 and 48 channels), each
    followed by max pooling by 4; the Hydra block runs four convolutions with
    kernels 3, 5, 9 and 15, 128 channels each, side by side; a 1 x 1
    convolution fuses their 512 channels into 128. The branches see 47, 63, 95
    and 143 ms of the input at 2000 Hz; about 279 thousand parameters for two
    channels and five classes.
    """

    def __init__(self, channels, classes, width=128):
        super().__init__()

        def unit(inputs, outputs, kernel):
            # The bias is redundant before batch normalization, which shifts
            # every channel anyway.
            return [
                nn.Conv1d(inputs, outputs, kernel_size=kernel, padding=kernel // 2, bias=False),
                nn.BatchNorm1d(outputs),
                nn.ReLU(),
            ]

        self.stem = nn.Sequential(
            *unit(channels, 32, 15), nn.MaxPool1d(4),
            *unit(32, 48, 9), nn.MaxPool1d(4),
        )
        self.branches = nn.ModuleList(nn.Sequential(*unit(48, width, kernel)) for kernel in (3, 5, 9, 15))
        self.fusion = nn.Sequential(*unit(4 * width, width, 1))
        self.head = nn.Sequential(
            nn.AdaptiveAvgPool1d(1),
            nn.Flatten(),
            nn.Dropout(0.2),
            nn.Linear(width, classes),
        )

    def forward(self, x):
        x = self.stem(x)
        x = torch.cat([branch(x) for branch in self.branches], dim=1)
        return self.head(self.fusion(x))


def _notch_hz(value):
    text = str(value if value is not None else "50").strip().lower()
    if text in ("", "off", "none", "0"):
        return 0.0
    return float(text)


def _filters(fs, notch_hz):
    """Second-order sections: 20-450 Hz band-pass and an optional mains notch."""
    if fs < 1000:
        raise ValueError(
            f"Gesture Hydra 1D-CNN requires sample_rate >= 1000 Hz for the 20-450 Hz "
            f"band-pass; got {fs} Hz"
        )
    sections = [butter(4, [20.0, 450.0], btype="bandpass", fs=float(fs), output="sos")]
    if notch_hz > 0.0:
        b, a = iirnotch(notch_hz, 30.0, fs=float(fs))
        sections.append(tf2sos(b, a))
    return np.vstack(sections)


def _filter(segments, sos):
    """Zero-phase filtering of segments shaped (..., channels, samples)."""
    return sosfiltfilt(sos, np.asarray(segments, dtype=np.float64), axis=-1).astype(np.float32)


def _read_dataset(dataset):
    with open(os.path.join(dataset, "manifest.json"), encoding="utf-8") as stream:
        manifest = json.load(stream)
    if manifest.get("schema_version") != DATASET_SCHEMA:
        raise ValueError("the dataset format version differs from the one this model reads")
    capture = manifest.get("capture", {})
    channels = len(capture.get("channels", []))
    if channels <= 0:
        raise ValueError("manifest capture.channels must contain at least one channel")
    sample_rate = int(capture.get("sample_rate", 0))
    window_samples = int(capture.get("window_samples", 0))
    if window_samples <= 0:
        raise ValueError("manifest capture.window_samples must be positive")

    # One example is one trial: one window from its offset in the signal file
    # of its session. Its target may have been corrected on the
    # Datasets page (the cue stays in "cue"); excluded trials stay on disk but
    # are not part of the selection.
    examples = []
    labels = set()
    with open(os.path.join(dataset, "examples.jsonl"), encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            item = json.loads(line)
            if item.get("excluded"):
                continue
            target = item.get("target", {})
            if not isinstance(target, dict) or "label" not in target:
                raise ValueError("gesture_cnn requires target.label for every trial")
            label = str(target["label"])
            relative_file = item.get("file")
            if not relative_file:
                raise ValueError("trial is missing file")
            if "offset" not in item:
                raise ValueError("trial is missing offset")
            offset = int(item["offset"])
            expected = window_samples * channels
            raw = np.fromfile(os.path.join(dataset, relative_file), dtype="<i4",
                              count=expected, offset=offset * channels * 4)
            if raw.size != expected:
                raise ValueError(
                    f"{relative_file}: trial {item.get('id')} at sample {offset} is cut short: "
                    f"expected {expected} int32 values, got {raw.size}"
                )
            # Channel-first, as the network takes it.
            values = raw.reshape(window_samples, channels).T.astype(np.float32)
            examples.append({"id": int(item.get("id", len(examples))), "label": label, "values": values})
            labels.add(label)

    if not examples:
        raise ValueError("no trials are left in the selection of the dataset")
    if len(labels) < 2:
        raise ValueError("fewer than two target values are left in the selection of the dataset")
    ordered_labels = sorted(labels)
    label_to_index = {label: index for index, label in enumerate(ordered_labels)}
    return manifest, examples, ordered_labels, label_to_index, channels, window_samples, sample_rate


def _split_stratified(examples, validation_split, seed):
    """Split the trials, before they are cut into segments, class by class.

    Every class gives the same share of its trials to the validation part:
    round(n * share), at least one and at most n - 1, so that each class is both
    learned and checked. The shuffle depends on the seed only.
    """
    by_label = {}
    for index, example in enumerate(examples):
        by_label.setdefault(example["label"], []).append(index)
    rng = random.Random(seed)
    train_indices, val_indices = [], []
    for label in sorted(by_label):
        indices = by_label[label][:]
        rng.shuffle(indices)
        count = 0
        if validation_split > 0.0 and len(indices) >= 2:
            count = min(max(int(round(len(indices) * validation_split)), 1), len(indices) - 1)
        val_indices += indices[:count]
        train_indices += indices[count:]
    return sorted(train_indices), sorted(val_indices)


def _write_json_atomic(obj, path):
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as stream:
        json.dump(obj, stream, ensure_ascii=False, indent=2)
    os.replace(temporary, path)


def _segments(examples, indices, label_to_index, window, hop, skip):
    """Cut every trial into windows of `window` samples every `hop` samples
    from the skip on."""
    values = []
    targets = []
    for index in indices:
        trial = examples[index]["values"]
        for start in range(skip, trial.shape[1] - window + 1, hop):
            values.append(trial[:, start:start + window])
            targets.append(label_to_index[examples[index]["label"]])
    return np.stack(values), np.asarray(targets, dtype=np.int64)


def _prepare(segments, sos, scale):
    """Filtering and scaling of segments shaped (..., channels, samples)."""
    return _filter(segments, sos) / scale[:, None]


def _set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def _save_atomic(obj, path):
    """Write a checkpoint so that an interruption never leaves half a file."""
    temporary = path + ".tmp"
    torch.save(obj, temporary)
    os.replace(temporary, path)


def train(dataset, output, report, config=None):
    config = config or {}
    (
        _manifest,
        examples,
        labels,
        label_to_index,
        channels,
        trial_samples,
        sample_rate,
    ) = _read_dataset(dataset)
    notch = _notch_hz(config.get("notch", "50"))
    sos = _filters(sample_rate, notch)
    # The same integer arithmetic as the application uses for the manifest.
    window = int(config.get("window_ms", 250)) * sample_rate // 1000
    hop = max(1, int(config.get("hop_ms", 62)) * sample_rate // 1000)
    skip = int(config.get("skip_ms", 500)) * sample_rate // 1000
    if window <= 0:
        raise ValueError("the model window must contain at least one sample")
    if skip + window > trial_samples:
        raise ValueError(
            f"the model window with the skip ({(skip + window) * 1000 // sample_rate} ms) "
            f"is longer than a trial of the dataset ({trial_samples * 1000 // sample_rate} ms)"
        )

    # Resuming: the checkpoint must come from the same network, data and
    # preprocessing, and its seed is reused so that the trials fall into the
    # same parts. The worker has already replaced a random seed (-1) by a value.
    checkpoint = None
    resume_path = str(config.get("resume") or "")
    if resume_path:
        if not os.path.exists(resume_path):
            raise ValueError(f"checkpoint not found: {resume_path}")
        checkpoint = torch.load(resume_path, map_location="cpu")
        if checkpoint.get("arch") != ARCH:
            raise ValueError("the checkpoint was made by another version of the network")
        for key, value in (("labels", labels), ("channels", channels),
                           ("sample_rate", sample_rate), ("window_samples", window),
                           ("skip_samples", skip), ("notch", notch)):
            saved = checkpoint.get(key)
            if (list(saved) if key == "labels" else saved) != value:
                raise ValueError(f"the checkpoint was trained on other data: {key} differs "
                                 f"({saved} instead of {value})")
    # A resumed training keeps its seed, hence its split.
    seed = int(checkpoint["seed"]) if checkpoint is not None else int(config.get("seed", -1))
    if seed < 0:
        raise ValueError("the seed is not set")
    _set_seed(seed)
    validation_split = float(config.get("validation_split", 0.2))
    train_indices, val_indices = _split_stratified(examples, validation_split, seed)
    if not train_indices:
        raise ValueError("the split produced an empty training part")

    # The windows are the same every epoch: they are filtered once.
    values, targets = _segments(examples, train_indices, label_to_index, window, hop, skip)
    filtered = _filter(values, sos)
    # Every channel is divided by its RMS over the training trials. Unlike
    # standardizing each window, this keeps both the level of activity and the
    # ratio between the channels, by which the gestures differ.
    if checkpoint is not None:
        scale = np.asarray(checkpoint["scale"], dtype=np.float32)
    else:
        scale = np.sqrt(np.mean(np.square(filtered, dtype=np.float64), axis=(0, 2))).astype(np.float32)
        scale[scale == 0.0] = 1.0
    train_set = TensorDataset(torch.from_numpy(filtered / scale[:, None]), torch.from_numpy(targets))
    del values, filtered

    val_tensor = None
    val_targets = None
    if val_indices:
        values, targets = _segments(examples, val_indices, label_to_index, window, hop, skip)
        val_tensor = torch.from_numpy(_prepare(values, sos, scale))
        val_targets = torch.from_numpy(targets)

    requested_device = str(config.get("device", "cpu"))
    if requested_device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but is not available")
    device = torch.device(requested_device)
    model = HydraCNN(channels, len(labels)).to(device)
    batch_size = max(1, int(config.get("batch_size", 256)))
    criterion = nn.CrossEntropyLoss()
    learning_rate = float(config.get("learning_rate", 1e-3))
    optimizer = torch.optim.AdamW(model.parameters(), lr=learning_rate,
                                  weight_decay=float(config.get("weight_decay", 1e-4)))
    epochs = max(1, int(config.get("epochs", 200)))
    best_val_accuracy = 0.0
    best_val_loss = float("inf")
    final_train_loss = 0.0
    final_val_loss = 0.0
    start_epoch = 1
    if checkpoint is not None:
        model.load_state_dict(checkpoint["state_dict"])
        # The optimizer state follows the parameters to their device.
        optimizer.load_state_dict(checkpoint["optimizer"])
        start_epoch = int(checkpoint["epoch"]) + 1
        best_val_loss = float(checkpoint.get("best_val_loss", float("inf")))
        best_val_accuracy = float(checkpoint.get("best_val_accuracy", 0.0))
        if start_epoch > epochs:
            raise ValueError(f"the model is already trained for {start_epoch - 1} epochs; "
                             f"set epochs above {start_epoch - 1} to continue")

    # Checkpoints are written after every epoch, so a stopped training keeps
    # the best weights so far (weights.pt) and the last epoch (last.pt).
    os.makedirs(output, exist_ok=True)
    # The seed and the trials per class in each part: with the hyperparameters
    # in the training history, enough to repeat the run on the same split.
    def counts(indices):
        result = {}
        for index in indices:
            result[examples[index]["label"]] = result.get(examples[index]["label"], 0) + 1
        return result

    training = {
        "seed": seed,
        "validation_split": validation_split,
        "trials": {"training": counts(train_indices), "validation": counts(val_indices)},
        "validation_ids": sorted(examples[index]["id"] for index in val_indices),
        "dataset": os.path.abspath(dataset),
    }
    _write_json_atomic(training, os.path.join(output, "training.json"))
    data_info = {"arch": ARCH, "labels": labels, "channels": channels,
                 "sample_rate": sample_rate, "window_samples": window,
                 "skip_samples": skip, "notch": notch,
                 "scale": [float(value) for value in scale]}

    for epoch in range(start_epoch, epochs + 1):
        # Cosine annealing from the initial rate to zero over the epochs; the
        # rate depends on the epoch only, so a resumed training follows it.
        epoch_rate = learning_rate * 0.5 * (1.0 + math.cos(math.pi * (epoch - 1) / epochs))
        for group in optimizer.param_groups:
            group["lr"] = epoch_rate
        # The shuffle depends on the seed and the epoch only, so a resumed
        # training shuffles the windows as the uninterrupted one would.
        loader = DataLoader(
            train_set,
            batch_size=batch_size,
            shuffle=True,
            generator=torch.Generator().manual_seed(seed * 100003 + epoch),
        )
        model.train()
        total_loss = 0.0
        total_items = 0
        for batch_values, batch_targets in loader:
            batch_values = batch_values.to(device)
            batch_targets = batch_targets.to(device)
            optimizer.zero_grad()
            loss = criterion(model(batch_values), batch_targets)
            loss.backward()
            optimizer.step()
            count = batch_values.shape[0]
            total_loss += float(loss.item()) * count
            total_items += count
        final_train_loss = total_loss / max(1, total_items)

        final_val_loss = 0.0
        val_accuracy = 0.0
        if val_tensor is not None:
            model.eval()
            with torch.no_grad():
                logits = model(val_tensor.to(device))
                final_val_loss = float(criterion(logits, val_targets.to(device)).item())
                val_accuracy = float((logits.argmax(dim=1) == val_targets.to(device)).float().mean().item())
        best_val_accuracy = max(best_val_accuracy, val_accuracy)
        # Without a validation part every epoch counts as the best one.
        improved = val_tensor is None or final_val_loss < best_val_loss
        if val_tensor is not None and improved:
            best_val_loss = final_val_loss
        state_dict = {name: value.detach().cpu() for name, value in model.state_dict().items()}
        if improved:
            _save_atomic({"state_dict": state_dict, "epoch": epoch, "val_loss": final_val_loss,
                          **data_info}, os.path.join(output, "weights.pt"))
        # The epoch goes into the history before the checkpoint: an interruption
        # in between repeats the epoch on resuming, and the history drops the
        # repeated one, so no epoch is missing from the chart.
        report(epoch, epochs, best=bool(improved), train_loss=final_train_loss,
               val_loss=final_val_loss, val_accuracy=val_accuracy, learning_rate=epoch_rate)
        _save_atomic({"state_dict": state_dict, "optimizer": optimizer.state_dict(), "epoch": epoch,
                      "best_val_loss": best_val_loss, "best_val_accuracy": best_val_accuracy,
                      "seed": seed, **data_info}, os.path.join(output, "last.pt"))

    return {
        "train_loss": final_train_loss,
        "val_loss": final_val_loss,
        "accuracy": best_val_accuracy,
        "seed": seed,
    }


def load(bundle):
    weights = torch.load(os.path.join(bundle, "weights.pt"), map_location="cpu")
    if weights.get("arch") != ARCH:
        raise ValueError("the model was trained by another version of the network; train it again")
    labels = list(weights["labels"])
    network = HydraCNN(int(weights["channels"]), len(labels))
    network.load_state_dict(weights["state_dict"])
    # Prefer CUDA for live inference, while keeping bundles runnable on hosts
    # where the CUDA runtime or a compatible GPU is unavailable.
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    network.to(device)
    network.eval()
    sample_rate = int(weights["sample_rate"])
    return {
        "network": network,
        "labels": labels,
        "channels": int(weights["channels"]),
        "window_samples": int(weights["window_samples"]),
        "sample_rate": sample_rate,
        "sos": _filters(sample_rate, float(weights["notch"])),
        "scale": np.asarray(weights["scale"], dtype=np.float32),
        "device": device,
    }


def predict(model, values, rows, channels):
    model_channels = int(model["channels"])
    channels = int(channels or model_channels)
    rows = int(rows or (len(values) // max(1, channels)))
    if channels != model_channels:
        raise ValueError(f"model expects {model_channels} channels, got {channels}")
    if rows != model["window_samples"]:
        raise ValueError(f"inference window has {rows} samples, "
                         f"the model expects {model['window_samples']}")
    expected = rows * channels
    if len(values) < expected:
        raise ValueError("inference window has insufficient interleaved samples")
    segment = np.asarray(values[:expected], dtype=np.float32).reshape(rows, channels).T
    prepared = _prepare(segment, model["sos"], model["scale"])
    tensor = torch.from_numpy(np.ascontiguousarray(prepared)).unsqueeze(0).to(model["device"])
    with torch.no_grad():
        probabilities = torch.softmax(model["network"](tensor), dim=1)[0]
    count = min(5, len(model["labels"]))
    top_probabilities, top_indices = torch.topk(probabilities, count)
    return {
        "kind": "top_k",
        "items": [
            {
                "label": model["labels"][int(index)],
                "probability": float(probability),
            }
            for probability, index in zip(top_probabilities, top_indices)
        ],
    }

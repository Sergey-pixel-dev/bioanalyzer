"""Parametric 1D CNN family for gesture and biosignal classification.

The dataset is intentionally kept raw.  This module owns the statistics,
outlier handling and conversion to the channel-first tensors expected by the
network, so training and inference use exactly the same numeric domain.
"""

import json
import os
import random

import numpy as np
from scipy.signal import butter, sosfiltfilt
import torch
from torch import nn
from torch.utils.data import DataLoader, TensorDataset


def meta():
    return {
        "name": "Gesture 1D-CNN",
        "task_type": "classification",
        "target_type": "label",
        "output": {"kind": "top_k", "max_k": 5},
        "hyperparameters": [
            {"name": "epochs", "type": "int", "min": 1, "max": 10000, "default": 50},
            {"name": "batch_size", "type": "int", "min": 1, "max": 4096, "default": 32},
            {
                "name": "learning_rate",
                "type": "float",
                "min": 1e-7,
                "max": 1.0,
                "default": 1e-3,
            },
            {
                "name": "validation_split",
                "type": "float",
                "min": 0.0,
                "max": 0.9,
                "default": 0.2,
            },
            {"name": "seed", "type": "int", "min": 0, "max": 100000, "default": 1},
            {
                "name": "device",
                "type": "enum",
                "choices": ["cpu", "cuda"],
                "default": "cpu",
            },
        ],
    }


class CNN1D(nn.Module):
    """The gesture network; channels and classes are inferred from a dataset."""

    def __init__(self, channels, classes):
        super().__init__()
        self.l1 = nn.Sequential(
            nn.Conv1d(channels, 32, kernel_size=10),
            nn.BatchNorm1d(32),
            nn.ReLU(),
        )
        self.l2 = nn.Sequential(
            nn.Conv1d(32, 64, kernel_size=7, stride=2),
            nn.BatchNorm1d(64),
            nn.ReLU(),
        )
        self.l3 = nn.Sequential(
            nn.Conv1d(64, 128, kernel_size=5, stride=2),
            nn.BatchNorm1d(128),
            nn.ReLU(),
        )
        self.l4 = nn.ModuleList(
            [
                nn.Sequential(
                    nn.Conv1d(128, 128, kernel_size=3, padding=1),
                    nn.BatchNorm1d(128),
                    nn.ReLU(),
                ),
                nn.Sequential(
                    nn.Conv1d(128, 128, kernel_size=5, padding=2),
                    nn.BatchNorm1d(128),
                    nn.ReLU(),
                ),
                nn.Sequential(
                    nn.Conv1d(128, 128, kernel_size=15, padding=7),
                    nn.BatchNorm1d(128),
                    nn.ReLU(),
                ),
            ]
        )
        self.l5 = nn.Sequential(
            nn.Conv1d(384, 64, kernel_size=1),
            nn.BatchNorm1d(64),
            nn.ReLU(),
        )
        self.head = nn.Sequential(
            nn.AdaptiveAvgPool1d(1),
            nn.Flatten(),
            nn.Dropout(0.3),
            nn.Linear(64, classes),
        )

    def forward(self, x):
        x = self.l1(x)
        x = self.l2(x)
        x = self.l3(x)
        x = torch.cat([branch(x) for branch in self.l4], dim=1)
        return self.head(self.l5(x))


def _transform(x, fs):
    """Apply the paper's per-window filter, denoising and normalization.

    Per-window z-score intentionally removes amplitude as a feature (section
    3.2.4 of the reference); rest/idle is therefore separated by waveform
    shape only.
    """
    if fs < 1000:
        raise ValueError(
            f"Gesture 1D-CNN requires sample_rate >= 1000 Hz for the 20-450 Hz "
            f"band-pass; got {fs} Hz (250-Hz datasets are not supported)"
        )
    values = np.asarray(x, dtype=np.float32)
    if values.ndim != 2:
        raise ValueError("_transform expects a (channels, samples) array")
    channels, samples = values.shape
    if channels <= 0 or samples <= 0:
        return np.zeros_like(values, dtype=np.float32)
    if samples < 4:
        raise ValueError("_transform needs at least four samples for zero-phase filtering")

    sos = butter(4, [20.0, 450.0], btype="bandpass", fs=float(fs), output="sos")
    filtered = np.empty_like(values, dtype=np.float32)
    for channel in range(channels):
        if np.all(values[channel] == values[channel, 0]):
            filtered[channel].fill(0.0)
        else:
            filtered[channel] = sosfiltfilt(sos, values[channel]).astype(
                np.float32, copy=False
            )

    denoised = filtered.copy()
    for channel in range(channels):
        window = filtered[channel]
        mean = float(window.mean(dtype=np.float64))
        std = float(window.std(dtype=np.float64))
        if std == 0.0:
            continue
        outliers = np.abs(window - mean) > 3.0 * std
        for index in np.flatnonzero(outliers):
            neighbours = []
            index = int(index)
            if index == 0:
                neighbours.append(float(window[1]))
            elif index == samples - 1:
                neighbours.append(float(window[samples - 2]))
            else:
                for distance in (1, 2):
                    left = index - distance
                    right = index + distance
                    if left >= 0:
                        neighbours.append(float(window[left]))
                    if right < samples:
                        neighbours.append(float(window[right]))
            denoised[channel, index] = (
                np.float32(np.mean(neighbours)) if neighbours else np.float32(mean)
            )

    mean2 = denoised.mean(axis=1, keepdims=True, dtype=np.float64).astype(np.float32)
    std2 = denoised.std(axis=1, keepdims=True, dtype=np.float64).astype(np.float32)
    result = np.zeros_like(denoised, dtype=np.float32)
    nonconstant = std2[:, 0] != 0.0
    if np.any(nonconstant):
        result[nonconstant] = (
            (denoised[nonconstant] - mean2[nonconstant]) / std2[nonconstant]
        ).astype(np.float32, copy=False)
    return result


def _read_dataset(dataset):
    with open(os.path.join(dataset, "manifest.json"), encoding="utf-8") as stream:
        manifest = json.load(stream)
    capture = manifest.get("capture", {})
    channel_ids = capture.get("channels", [])
    channels = len(channel_ids)
    if channels <= 0:
        raise ValueError("manifest capture.channels must contain at least one channel")
    sample_rate = int(capture.get("sample_rate", 0))
    window_samples = int(capture.get("window_samples", 0))
    paradigm = manifest.get("task", {}).get("paradigm", {})
    prep_seconds = float(paradigm.get("prep_s", 0))
    record_seconds = float(paradigm.get("record_s", 0))
    cycle_samples = int(round((prep_seconds + record_seconds) * sample_rate))

    examples = []
    labels = set()
    with open(os.path.join(dataset, "examples.jsonl"), encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            item = json.loads(line)
            target = item.get("target", {})
            if not isinstance(target, dict) or "label" not in target:
                raise ValueError("gesture_cnn requires target.label for every example")
            label = str(target["label"])
            relative_file = item.get("file")
            if not relative_file:
                raise ValueError("example is missing file")
            samples = int(item.get("samples", window_samples))
            if samples <= 0:
                raise ValueError("example samples must be positive")
            path = os.path.join(dataset, relative_file)
            raw = np.fromfile(path, dtype="<i4")
            expected = samples * channels
            if raw.size != expected:
                raise ValueError(
                    f"{relative_file}: expected {expected} int32 values, got {raw.size}"
                )
            time_major = raw.reshape(samples, channels).astype(np.float32)
            if window_samples and samples != window_samples:
                raise ValueError(
                    f"{relative_file}: samples={samples} differs from manifest window_samples={window_samples}"
                )
            source_sample = int(item.get("source_sample", 0))
            group = source_sample // cycle_samples if cycle_samples > 0 else len(examples)
            examples.append({"label": label, "values": time_major, "group": group})
            labels.add(label)

    if not examples:
        raise ValueError("dataset contains no examples")
    if not window_samples:
        window_samples = examples[0]["values"].shape[0]
    ordered_labels = sorted(labels)
    label_to_index = {label: index for index, label in enumerate(ordered_labels)}
    return manifest, examples, ordered_labels, label_to_index, channels, window_samples, cycle_samples


def _split_by_groups(examples, validation_split, seed):
    groups = {}
    for index, example in enumerate(examples):
        groups.setdefault(example["group"], []).append(index)
    group_ids = list(groups)
    if len(group_ids) <= 1 or validation_split <= 0.0:
        return list(range(len(examples))), []

    rng = random.Random(seed)
    rng.shuffle(group_ids)
    val_count = int(round(len(group_ids) * validation_split))
    val_count = max(1, min(len(group_ids) - 1, val_count))
    val_groups = set(group_ids[:val_count])
    train_groups = set(group_ids[val_count:])

    # Never remove the only cycle of a class from training.
    class_groups = {}
    for group_id, indices in groups.items():
        class_groups.setdefault(examples[indices[0]]["label"], []).append(group_id)
    for class_group_ids in class_groups.values():
        if not (train_groups & set(class_group_ids)):
            moved = next(iter(val_groups & set(class_group_ids)), None)
            if moved is not None:
                val_groups.remove(moved)
                train_groups.add(moved)

    train_indices = [index for group in train_groups for index in groups[group]]
    val_indices = [index for group in val_groups for index in groups[group]]
    return sorted(train_indices), sorted(val_indices)


def _set_seed(seed):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def train(dataset, output, report, config=None):
    config = config or {}
    (
        _manifest,
        examples,
        labels,
        label_to_index,
        channels,
        window_samples,
        cycle_samples,
    ) = _read_dataset(dataset)
    sample_rate = int(_manifest.get("capture", {}).get("sample_rate", 0))
    if sample_rate < 1000:
        raise ValueError(
            f"Gesture 1D-CNN requires sample_rate >= 1000 Hz for the 20-450 Hz "
            f"band-pass; got {sample_rate} Hz (250-Hz datasets are not supported)"
        )
    seed = int(config.get("seed", 1))
    _set_seed(seed)
    train_indices, val_indices = _split_by_groups(
        examples, float(config.get("validation_split", 0.2)), seed
    )
    if not train_indices:
        raise ValueError("group split produced an empty training set")

    train_values = np.stack(
        [_transform(examples[index]["values"].T, sample_rate) for index in train_indices]
    )
    train_tensor = torch.from_numpy(train_values)
    train_targets = torch.tensor(
        [label_to_index[examples[index]["label"]] for index in train_indices], dtype=torch.long
    )

    val_tensor = None
    val_targets = None
    if val_indices:
        val_values = np.stack(
            [_transform(examples[index]["values"].T, sample_rate) for index in val_indices]
        )
        val_tensor = torch.from_numpy(val_values)
        val_targets = torch.tensor(
            [label_to_index[examples[index]["label"]] for index in val_indices], dtype=torch.long
        )

    requested_device = str(config.get("device", "cpu"))
    if requested_device == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but is not available")
    device = torch.device(requested_device)
    model = CNN1D(channels, len(labels)).to(device)
    loader = DataLoader(
        TensorDataset(train_tensor, train_targets),
        batch_size=max(1, int(config.get("batch_size", 32))),
        shuffle=True,
        generator=torch.Generator().manual_seed(seed),
    )
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(
        model.parameters(), lr=float(config.get("learning_rate", 1e-3))
    )
    epochs = max(1, int(config.get("epochs", 50)))
    best_val_accuracy = 0.0
    final_train_loss = 0.0
    final_val_loss = 0.0

    for epoch in range(1, epochs + 1):
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
        report(epoch, epochs, final_train_loss, final_val_loss, val_accuracy)

    os.makedirs(output, exist_ok=True)
    state_dict = {name: value.detach().cpu() for name, value in model.state_dict().items()}
    torch.save(
        {
            "state_dict": state_dict,
            "labels": labels,
            "channels": channels,
            "window_samples": window_samples,
            "sample_rate": sample_rate,
        },
        os.path.join(output, "weights.pt"),
    )
    return {
        "train_loss": final_train_loss,
        "val_loss": final_val_loss,
        "accuracy": best_val_accuracy,
    }


def load(bundle):
    weights = torch.load(os.path.join(bundle, "weights.pt"), map_location="cpu")
    labels = list(weights["labels"])
    network = CNN1D(int(weights["channels"]), len(labels))
    network.load_state_dict(weights["state_dict"])
    # Prefer CUDA for live inference, while keeping bundles runnable on hosts
    # where the CUDA runtime or a compatible GPU is unavailable.
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    network.to(device)
    network.eval()
    sample_rate = int(weights.get("sample_rate", 0))
    manifest_path = os.path.join(bundle, "manifest.json")
    if os.path.exists(manifest_path):
        with open(manifest_path, encoding="utf-8") as stream:
            manifest = json.load(stream)
        capture = manifest.get("capture", {})
        sample_rate = int(capture.get("sample_rate", manifest.get("sample_rate", sample_rate)))
    return {
        "network": network,
        "labels": labels,
        "channels": int(weights["channels"]),
        "window_samples": int(weights.get("window_samples", 0)),
        "sample_rate": sample_rate,
        "device": device,
    }


def predict(model, values, rows, channels):
    model_channels = int(model["channels"])
    channels = int(channels or model_channels)
    rows = int(rows or (len(values) // max(1, channels)))
    if channels != model_channels:
        raise ValueError(f"model expects {model_channels} channels, got {channels}")
    expected = rows * channels
    if rows <= 0 or len(values) < expected:
        raise ValueError("inference window has insufficient interleaved samples")
    time_major = np.asarray(values[:expected], dtype=np.float32).reshape(rows, channels)
    transformed = _transform(time_major.T, model["sample_rate"])
    tensor = torch.from_numpy(transformed.copy()).unsqueeze(0).to(model["device"])
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

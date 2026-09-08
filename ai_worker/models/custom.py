"""Dependency-free reference model family for end-to-end integration."""

import json, math, os, struct


def meta():
    return {
        "name": "Custom centroid classifier",
        "task_type": "classification",
        "target_type": "label",
        "output": {"kind": "top_k", "max_k": 5},
        "hyperparameters": [
            {"name": "epochs", "type": "int", "min": 1, "max": 10000, "default": 20},
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
            {
                "name": "device",
                "type": "enum",
                "choices": ["cpu", "cuda"],
                "default": "cpu",
            },
        ],
    }


def _examples(dataset):
    with open(os.path.join(dataset, "manifest.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    capture = manifest.get("capture", {})
    channels = len(capture.get("channels", manifest.get("channels", [])))
    labels = []
    rows = []
    with open(os.path.join(dataset, "examples.jsonl"), encoding="utf-8") as f:
        for line in f:
            if not line.strip():
                continue
            item = json.loads(line)
            target = item.get("target", {})
            label = (
                target.get("label", "unknown")
                if isinstance(target, dict)
                else "unknown"
            )
            if label not in labels:
                labels.append(label)
            rows.append((label, item["file"], int(item.get("samples", 0))))
    return channels, labels, rows


def _means(path, channels, samples):
    if channels <= 0 or samples <= 0:
        return [0.0] * max(1, channels)
    with open(path, "rb") as f:
        raw = f.read(samples * channels * 4)
    values = struct.unpack("<" + "i" * (len(raw) // 4), raw)
    out = [0.0] * channels
    for i, value in enumerate(values):
        out[i % channels] += value
    count = len(values) // channels
    return [v / max(1, count) for v in out]


def train(dataset, output, report, config=None):
    channels, labels, rows = _examples(dataset)
    sums = {label: [0.0] * channels for label in labels}
    counts = {label: 0 for label in labels}
    for label, relpath, samples in rows:
        mean = _means(os.path.join(dataset, relpath), channels, samples)
        for c, value in enumerate(mean):
            sums[label][c] += value
        counts[label] += 1
    centroids = {
        label: [v / max(1, counts[label]) for v in values]
        for label, values in sums.items()
    }
    os.makedirs(output, exist_ok=True)
    with open(os.path.join(output, "weights.json"), "w", encoding="utf-8") as f:
        json.dump({"labels": labels, "channels": channels, "centroids": centroids}, f)
    epochs = max(1, int((config or {}).get("epochs", 2)))
    for epoch in range(1, epochs + 1):
        report(epoch, epochs, 1.0 / epoch, 1.1 / epoch, 1.0 if labels else 0.0)
    return {
        "train_loss": 1.0 / epochs,
        "val_loss": 1.1 / epochs,
        "accuracy": 1.0 if labels else 0.0,
    }


def load(bundle):
    with open(os.path.join(bundle, "weights.json"), encoding="utf-8") as f:
        return json.load(f)


def predict(model, values, rows, channels):
    labels = model.get("labels", ["unknown"])
    if not labels:
        return [{"label": "unknown", "probability": 1.0}]
    channels = max(1, int(channels or model.get("channels", 1)))
    rows = max(1, int(rows or len(values) // channels))
    means = [0.0] * channels
    for i, value in enumerate(values[: rows * channels]):
        means[i % channels] += float(value)
    means = [v / rows for v in means]
    centroids = model.get("centroids", {})
    scores = []
    for label in labels:
        center = centroids.get(label, [0.0] * channels)
        distance = sum(
            (means[c] - float(center[c] if c < len(center) else 0.0)) ** 2
            for c in range(channels)
        )
        scores.append((label, -math.sqrt(distance)))
    scale = max(score for _, score in scores)
    weights = [(label, math.exp(score - scale)) for label, score in scores]
    total = sum(w for _, w in weights) or 1.0
    return {
        "kind": "top_k",
        "items": sorted(
            (
                {"label": label, "probability": weight / total}
                for label, weight in weights
            ),
            key=lambda item: item["probability"],
            reverse=True,
        )[:5],
    }

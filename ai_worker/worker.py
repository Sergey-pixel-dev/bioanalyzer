#!/usr/bin/env python3
"""Protocol 3 JSON-lines worker for arbitrary model modules.

The worker is the same for every model: it loads the module, resolves the
hyperparameters left random, keeps the training history in the model folder
and turns what the module returns into protocol messages.
"""

import argparse, importlib, json, os, random, sys

PROTOCOL = 3
# Version of the training history (history.json in the model folder).
HISTORY_SCHEMA = 1


def emit(obj):
    sys.stdout.write(
        json.dumps({"protocol": PROTOCOL, **obj}, ensure_ascii=False) + "\n"
    )
    sys.stdout.flush()


def load_family(name, models_dir=None):
    if models_dir and models_dir not in sys.path:
        sys.path.insert(0, models_dir)
    try:
        return importlib.import_module(f"models.{name}")
    except Exception as exc:
        emit({"type": "error", "message": f"Cannot load model family '{name}': {exc}"})
        return None


def write_json_atomic(obj, path):
    """Write so that an interruption never leaves half a file."""
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as stream:
        json.dump(obj, stream, ensure_ascii=False, indent=1)
    os.replace(temporary, path)


def resolve_random(meta, config):
    """A hyperparameter declared with "special" and left at its minimum means
    "random": it gets a value here, so that the history keeps the value used."""
    config = dict(config)
    for parameter in meta.get("hyperparameters", []):
        name = parameter.get("name")
        if "special" not in parameter or name not in config:
            continue
        low = int(parameter.get("min", 0))
        if int(config[name]) <= low:
            config[name] = random.SystemRandom().randint(low + 1, int(parameter.get("max", 2**31 - 1)))
    return config


class History:
    """history.json: the hyperparameters, the values the model reports and
    their values by epoch. A resumed training keeps the earlier epochs; its
    first report drops the epochs from the reported one on, so that an epoch
    reported before an interruption is not repeated."""

    def __init__(self, output, family, dataset, config, outputs):
        self.path = os.path.join(output, "history.json")
        epochs = []
        self.trimmed = True
        if config.get("resume"):
            if not os.path.exists(self.path):
                raise ValueError("the model has no training history to continue")
            with open(self.path, encoding="utf-8") as stream:
                previous = json.load(stream)
            if previous.get("schema_version") != HISTORY_SCHEMA:
                raise ValueError("the training history format version differs from the one this program reads")
            epochs = previous.get("epochs", [])
            self.trimmed = False
        self.data = {
            "schema_version": HISTORY_SCHEMA,
            "family": family,
            "dataset": os.path.abspath(dataset),
            "hyperparameters": {k: v for k, v in config.items() if k != "resume"},
            "outputs": outputs,
            "planned_epochs": 0,
            "epochs": epochs,
        }
        os.makedirs(output, exist_ok=True)
        write_json_atomic(self.data, self.path)

    def add(self, epoch, total, values, best):
        if not self.trimmed:
            self.data["epochs"] = [e for e in self.data["epochs"] if int(e.get("epoch", 0)) < epoch]
            self.trimmed = True
        self.data["planned_epochs"] = total
        self.data["epochs"].append({"epoch": epoch, "values": values, "best": best})
        write_json_atomic(self.data, self.path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", required=True, choices=("describe", "train", "infer"))
    ap.add_argument("--dataset")
    ap.add_argument("--output")
    ap.add_argument("--model")
    ap.add_argument("--family", required=True)
    ap.add_argument("--models-dir")
    ap.add_argument("--config", default="{}")
    args = ap.parse_args()
    family = load_family(args.family, args.models_dir)
    if family is None:
        return 2
    if args.mode == "describe":
        if not hasattr(family, "meta"):
            emit({"type": "error", "message": "Model family does not export meta()"})
            return 2
        try:
            emit({"type": "meta", "family": args.family, "meta": family.meta()})
            return 0
        except Exception as exc:
            emit({"type": "error", "message": str(exc)})
            return 1
    if args.mode == "train":
        if not args.dataset or not args.output:
            emit({"type": "error", "message": "Dataset and output are required"})
            return 2
        try:
            meta = family.meta()
            config = resolve_random(meta, json.loads(args.config))
            history = History(args.output, args.family, args.dataset, config,
                              meta.get("outputs", {}).get("training", []))

            # The model reports after every epoch the values its description
            # declares in outputs.training; "best" marks the epoch whose
            # weights are kept for recognition.
            def report(epoch, total, best=False, **values):
                values = {name: float(value) for name, value in values.items()}
                history.add(int(epoch), int(total), values, bool(best))
                emit({"type": "metric", "epoch": int(epoch), "epochs": int(total),
                      "values": values, "best": bool(best)})

            result = family.train(args.dataset, args.output, report, config)
            emit(
                {
                    "type": "result",
                    "output": {
                        "kind": "model",
                        "path": args.output,
                        "metrics": result or {},
                    },
                }
            )
            return 0
        except Exception as exc:
            emit({"type": "error", "message": str(exc)})
            return 1
    if not args.model:
        emit({"type": "error", "message": "Model bundle is required"})
        return 2
    try:
        model = family.load(args.model)
    except Exception as exc:
        emit({"type": "error", "message": str(exc)})
        return 1
    for line in sys.stdin:
        try:
            cmd = json.loads(line)
            if cmd.get("cmd") == "stop":
                return 0
            if cmd.get("cmd") == "window":
                output = family.predict(
                    model,
                    cmd.get("values", []),
                    int(cmd.get("rows", 0)),
                    int(cmd.get("channels", 0)),
                )
                # The kind of the answer is the one the description declares in
                # outputs.inference; the application checks it.
                emit({"type": "result", "output": output})
        except Exception as exc:
            emit({"type": "error", "message": str(exc)})
    return 0


if __name__ == "__main__":
    sys.exit(main())

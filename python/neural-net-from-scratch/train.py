#!/usr/bin/env python3
"""
CLI training script for the from-scratch feedforward network.

Usage:
    python3 train.py
    python3 train.py --dataset blobs --epochs 3000 --lr 0.3 --hidden 8
    python3 train.py --dataset xor --loss mse --hidden 4 --epochs 2000

Builds a small multi-layer perceptron (Dense -> Tanh -> Dense -> Tanh ->
Dense -> Sigmoid), trains it with vanilla full-batch gradient descent on
one of the synthetic datasets from data.py (generated locally, nothing
downloaded), prints the training loss curve, and reports final train/test
accuracy. No ML framework is used anywhere -- forward pass, backprop, and
the weight updates are all implemented from scratch in layers.py /
network.py; numpy here is just doing matrix arithmetic.
"""

from __future__ import annotations

import argparse
import time

import numpy as np

from data import make_blobs_dataset, make_xor_dataset, train_test_split
from layers import Dense, ReLU, Sigmoid, Tanh
from network import LOSSES, Network
from visualize import render_decision_boundary


ACTIVATIONS = {"tanh": Tanh, "relu": ReLU}


def build_network(input_dim: int, hidden_dim: int, learning_rate: float, seed: int,
                    activation: str = "tanh") -> Network:
    rng = np.random.default_rng(seed)
    hidden_activation = ACTIVATIONS[activation]
    return Network([
        Dense(input_dim, hidden_dim, learning_rate=learning_rate, rng=rng),
        hidden_activation(),
        Dense(hidden_dim, hidden_dim, learning_rate=learning_rate, rng=rng),
        hidden_activation(),
        Dense(hidden_dim, 1, learning_rate=learning_rate, rng=rng),
        Sigmoid(),
    ])


def accuracy(y_pred: np.ndarray, y_true: np.ndarray, threshold: float = 0.5) -> float:
    predicted_labels = (y_pred >= threshold).astype(float)
    return float(np.mean(predicted_labels == y_true))


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train a from-scratch feedforward neural network.")
    parser.add_argument("--dataset", choices=["xor", "blobs"], default="xor",
                         help="which synthetic dataset to train on (default: xor)")
    parser.add_argument("--epochs", type=int, default=4000, help="training epochs (default: 4000)")
    parser.add_argument("--lr", type=float, default=0.5, help="gradient descent learning rate (default: 0.5)")
    parser.add_argument("--hidden", type=int, default=8, help="units per hidden layer (default: 8)")
    parser.add_argument("--activation", choices=sorted(ACTIVATIONS), default="tanh",
                         help="hidden layer activation function (default: tanh)")
    parser.add_argument("--seed", type=int, default=0, help="random seed, for reproducibility (default: 0)")
    parser.add_argument("--loss", choices=sorted(LOSSES), default="bce", help="loss function (default: bce)")
    parser.add_argument("--noise", type=float, default=None,
                         help="dataset noise stddev (default: 0.05 for xor, 1.0 for blobs)")
    parser.add_argument("--log-every", type=int, default=None,
                         help="print loss every N epochs (default: epochs // 10)")
    parser.add_argument("--no-plot", dest="plot", action="store_false", default=True,
                         help="skip the ASCII decision-boundary visualization")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    if args.dataset == "xor":
        noise = args.noise if args.noise is not None else 0.05
        x, y = make_xor_dataset(n_samples_per_point=64, noise=noise, seed=args.seed)
    else:
        noise = args.noise if args.noise is not None else 1.0
        x, y = make_blobs_dataset(n_samples=400, noise=noise, seed=args.seed)

    x_train, y_train, x_test, y_test = train_test_split(x, y, test_ratio=0.2, seed=args.seed)

    net = build_network(input_dim=x.shape[1], hidden_dim=args.hidden,
                         learning_rate=args.lr, seed=args.seed, activation=args.activation)
    loss_fn = LOSSES[args.loss]
    log_every = args.log_every or max(1, args.epochs // 10)

    print(f"dataset={args.dataset}  train_samples={len(x_train)}  test_samples={len(x_test)}")
    print(f"network={net}")
    print(f"parameters={net.num_parameters()}  lr={args.lr}  epochs={args.epochs}  loss={args.loss}")
    print("-" * 78)

    history: list[float] = []
    start = time.time()
    for epoch in range(1, args.epochs + 1):
        loss = net.train_step(x_train, y_train, loss_fn)
        history.append(loss)
        if epoch == 1 or epoch % log_every == 0 or epoch == args.epochs:
            train_acc = accuracy(net.predict(x_train), y_train)
            print(f"epoch {epoch:5d}/{args.epochs}   loss={loss:.6f}   train_acc={train_acc * 100:6.2f}%")
    elapsed = time.time() - start

    final_train_acc = accuracy(net.predict(x_train), y_train)
    final_test_acc = accuracy(net.predict(x_test), y_test)

    print("-" * 78)
    print(f"training complete in {elapsed:.2f}s ({args.epochs / max(elapsed, 1e-9):.0f} epochs/sec)")
    print(f"initial loss:    {history[0]:.6f}")
    print(f"final loss:      {history[-1]:.6f}")
    print(f"loss reduction:  {(1 - history[-1] / history[0]) * 100:.2f}%")
    print(f"train accuracy:  {final_train_acc * 100:.2f}%  ({len(x_train)} samples)")
    print(f"test accuracy:   {final_test_acc * 100:.2f}%  ({len(x_test)} samples)")

    if args.dataset == "xor":
        print()
        print("XOR truth table check (trained network, not the training set):")
        table = np.array([[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]])
        predictions = net.predict(table)
        all_correct = True
        for inputs, pred in zip(table, predictions):
            expected = int(inputs[0]) ^ int(inputs[1])
            got = int(pred[0] >= 0.5)
            ok = got == expected
            all_correct &= ok
            status = "OK" if ok else "MISMATCH"
            print(f"  {int(inputs[0])} XOR {int(inputs[1])} = {expected}   "
                  f"predicted={got}  (p={pred[0]:.4f})   [{status}]")
        print()
        print("ALL XOR CASES CORRECT" if all_correct else "SOME XOR CASES INCORRECT")

    if args.plot:
        print()
        print("Decision boundary learned by the trained network:")
        print(render_decision_boundary(net, x, y))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

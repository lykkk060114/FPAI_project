#!/usr/bin/env python3
"""Train and export the four-class panda behavior DeiT-Tiny model."""

from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

import timm
import torch
from timm.layers import set_fused_attn
from torch import nn
from torch.amp import GradScaler, autocast
from torch.utils.data import DataLoader
from torchvision import datasets, transforms


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=40)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--weight-decay", type=float, default=0.05)
    parser.add_argument("--patience", type=int, default=10)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--model", default="deit_tiny_patch16_224")
    parser.add_argument("--no-pretrained", action="store_true")
    return parser.parse_args()


def seed_everything(seed: int) -> None:
    random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


def macro_f1(predictions: torch.Tensor, targets: torch.Tensor,
             class_count: int) -> float:
    scores = []
    for class_id in range(class_count):
        predicted = predictions == class_id
        actual = targets == class_id
        tp = (predicted & actual).sum().item()
        fp = (predicted & ~actual).sum().item()
        fn = (~predicted & actual).sum().item()
        denominator = 2 * tp + fp + fn
        scores.append(2 * tp / denominator if denominator else 0.0)
    return sum(scores) / len(scores)


def evaluate(model: nn.Module, loader: DataLoader, criterion: nn.Module,
             device: torch.device, class_count: int) -> tuple[float, float, float]:
    model.eval()
    loss_sum = 0.0
    predictions = []
    targets = []
    with torch.inference_mode():
        for images, labels in loader:
            images = images.to(device, non_blocking=True)
            labels = labels.to(device, non_blocking=True)
            logits = model(images)
            loss_sum += criterion(logits, labels).item() * images.size(0)
            predictions.append(logits.argmax(dim=1).cpu())
            targets.append(labels.cpu())
    prediction_tensor = torch.cat(predictions)
    target_tensor = torch.cat(targets)
    accuracy = (prediction_tensor == target_tensor).float().mean().item()
    return (loss_sum / len(loader.dataset), accuracy,
            macro_f1(prediction_tensor, target_tensor, class_count))


def export_onnx(model: nn.Module, output: Path, class_names: list[str],
                architecture: str = "deit_tiny_patch16_224") -> None:
    model = model.cpu().eval()
    dummy = torch.zeros(1, 3, 224, 224)
    torch.onnx.export(
        model,
        dummy,
        output,
        input_names=["images"],
        output_names=["logits"],
        opset_version=13,
        do_constant_folding=True,
        dynamic_axes=None,
    )
    output.with_suffix(".json").write_text(
        json.dumps({
            "architecture": architecture,
            "input_shape": [1, 3, 224, 224],
            "color_order": "RGB",
            "resize": "preserve_aspect_ratio_center_pad",
            "padding_value": 127,
            "output": "logits",
            "normalization": {
                "mean": [0.485, 0.456, 0.406],
                "std": [0.229, 0.224, 0.225],
            },
            "classes": class_names,
        }, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> None:
    args = parse_args()
    if args.epochs < 1 or args.batch_size < 1 or args.patience < 1 or args.workers < 0:
        raise ValueError("epochs, batch-size, patience must be positive; workers must be nonnegative")
    seed_everything(args.seed)
    # Keep the exported graph on basic MatMul/Softmax ops for iCraft 3.36.
    set_fused_attn(False)
    args.output.mkdir(parents=True, exist_ok=True)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    train_transform = transforms.Compose([
        # Gentle crop: the water/food context determines the behavior label.
        transforms.RandomResizedCrop(224, scale=(0.90, 1.0), ratio=(0.95, 1.05)),
        transforms.RandomHorizontalFlip(),
        transforms.ColorJitter(brightness=0.2, contrast=0.2,
                               saturation=0.15, hue=0.03),
        transforms.RandomRotation(8),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406],
                             [0.229, 0.224, 0.225]),
    ])
    val_transform = transforms.Compose([
        transforms.Resize((224, 224)),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406],
                             [0.229, 0.224, 0.225]),
    ])
    train_dataset = datasets.ImageFolder(args.data / "train", train_transform)
    val_dataset = datasets.ImageFolder(args.data / "val", val_transform)
    if train_dataset.classes != val_dataset.classes:
        raise RuntimeError("train and val classes differ")
    if train_dataset.classes != ["drink", "eat", "rest", "walk"]:
        raise RuntimeError("expected drink/eat/rest/walk in both train and val")

    counts = torch.bincount(torch.tensor(train_dataset.targets),
                            minlength=len(train_dataset.classes)).float()
    class_weights = counts.sum() / (len(counts) * counts.clamp_min(1.0))
    criterion = nn.CrossEntropyLoss(weight=class_weights.to(device),
                                    label_smoothing=0.1)
    model = timm.create_model(
        args.model,
        pretrained=not args.no_pretrained,
        num_classes=len(train_dataset.classes),
    ).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr,
                                  weight_decay=args.weight_decay)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(
        optimizer, T_max=max(1, args.epochs))
    scaler = GradScaler("cuda", enabled=device.type == "cuda")

    train_loader = DataLoader(
        train_dataset, batch_size=args.batch_size, shuffle=True,
        num_workers=args.workers, pin_memory=device.type == "cuda",
        persistent_workers=args.workers > 0,
    )
    val_loader = DataLoader(
        val_dataset, batch_size=args.batch_size, shuffle=False,
        num_workers=args.workers, pin_memory=device.type == "cuda",
        persistent_workers=args.workers > 0,
    )

    print(json.dumps({
        "device": str(device),
        "classes": train_dataset.classes,
        "train_images": len(train_dataset),
        "val_images": len(val_dataset),
        "class_counts": counts.int().tolist(),
        "class_weights": class_weights.tolist(),
    }, ensure_ascii=False))

    best_f1 = -1.0
    stale_epochs = 0
    history = []
    checkpoint_path = args.output / "deit_tiny_panda_behavior_best.pt"
    for epoch in range(1, args.epochs + 1):
        model.train()
        train_loss = 0.0
        for images, labels in train_loader:
            images = images.to(device, non_blocking=True)
            labels = labels.to(device, non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            with autocast("cuda", enabled=device.type == "cuda"):
                logits = model(images)
                loss = criterion(logits, labels)
            scaler.scale(loss).backward()
            scaler.step(optimizer)
            scaler.update()
            train_loss += loss.item() * images.size(0)
        scheduler.step()
        val_loss, val_accuracy, val_f1 = evaluate(
            model, val_loader, criterion, device, len(train_dataset.classes))
        row = {
            "epoch": epoch,
            "train_loss": train_loss / len(train_dataset),
            "val_loss": val_loss,
            "val_accuracy": val_accuracy,
            "val_macro_f1": val_f1,
            "lr": optimizer.param_groups[0]["lr"],
        }
        history.append(row)
        print(json.dumps(row))
        (args.output / "metrics.json").write_text(
            json.dumps(history, indent=2) + "\n", encoding="utf-8")
        if val_f1 > best_f1:
            best_f1 = val_f1
            stale_epochs = 0
            torch.save({
                "model": model.state_dict(),
                "architecture": args.model,
                "classes": train_dataset.classes,
                "epoch": epoch,
                "val_macro_f1": val_f1,
                "val_accuracy": val_accuracy,
            }, checkpoint_path)
        else:
            stale_epochs += 1
            if stale_epochs >= args.patience:
                print(f"early stopping at epoch {epoch}")
                break

    checkpoint = torch.load(checkpoint_path, map_location="cpu", weights_only=True)
    model = timm.create_model(checkpoint["architecture"], pretrained=False,
                              num_classes=len(checkpoint["classes"]))
    model.load_state_dict(checkpoint["model"])
    export_onnx(model, args.output / "deit_tiny_panda_behavior.onnx",
                checkpoint["classes"], checkpoint["architecture"])
    print(json.dumps({
        "best_epoch": checkpoint["epoch"],
        "best_val_macro_f1": checkpoint["val_macro_f1"],
        "best_val_accuracy": checkpoint["val_accuracy"],
        "onnx": str(args.output / "deit_tiny_panda_behavior.onnx"),
    }))


if __name__ == "__main__":
    main()

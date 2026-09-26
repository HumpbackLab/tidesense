#!/usr/bin/env python3
"""Export the FAST-compatible XFeat dense descriptor map for CV181x."""

import argparse
import sys
from pathlib import Path

import cv2
import numpy as np
import torch
from torch import nn


class DescriptorBackbone(nn.Module):
    def __init__(self, net, with_heads=False):
        super().__init__()
        self.net = net
        self.with_heads = with_heads

    def forward(self, x):
        # Input is grayscale already normalized on the A53. Sparse XFeat
        # matching samples descriptors from this H/8 x W/8 feature map.
        n = self.net
        x1 = n.block1(x)
        x2 = n.block2(x1 + n.skip1(x))
        x3 = n.block3(x2)
        x4 = n.block4(x3)
        x5 = n.block5(x4)
        x4 = torch.nn.functional.interpolate(x4, x3.shape[-2:], mode="bilinear")
        x5 = torch.nn.functional.interpolate(x5, x3.shape[-2:], mode="bilinear")
        features = n.block_fusion(x3 + x4 + x5)
        if self.with_heads:
            return features, n.keypoint_head(n._unfold2d(x, ws=8)), n.heatmap_head(features)
        return features


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--xfeat", type=Path, required=True)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--with-heads", action="store_true")
    args = p.parse_args()
    sys.path.insert(0, str(args.xfeat))
    from modules.xfeat import XFeat

    torch.set_num_threads(1)
    xfeat = XFeat()
    image = cv2.imread(str(args.image), cv2.IMREAD_GRAYSCALE)
    if image is None:
        raise FileNotFoundError(args.image)
    image = cv2.equalizeHist(image)
    h, w = image.shape
    image = cv2.resize(image, (w // 32 * 32, h // 32 * 32), interpolation=cv2.INTER_LINEAR)
    raw = torch.from_numpy(image.copy()).float()[None, None]
    normalized = xfeat.net.norm(raw)
    backbone = DescriptorBackbone(xfeat.net, args.with_heads).eval()
    with torch.inference_mode():
        original = xfeat.net(raw)
        split = backbone(normalized)
        reference = original if args.with_heads else original[0]
        error = max(float((a - b).abs().max()) for a, b in zip(reference, split)) if args.with_heads else float((reference - split).abs().max())
        print("split-versus-official max abs error:", error)
        if error > 1e-4:
            raise RuntimeError("split graph differs from upstream XFeat")
    args.out.mkdir(parents=True, exist_ok=True)
    filename = "xfeat_full_frontend.onnx" if args.with_heads else "xfeat_backbone.onnx"
    output_names = ["descriptor_map", "keypoint_logits", "reliability"] if args.with_heads else ["descriptor_map"]
    torch.onnx.export(backbone, normalized, str(args.out / filename),
                      input_names=["image_norm"], output_names=output_names, opset_version=13)
    np.save(args.out / "input_normalized.npy", normalized.numpy())
    if args.with_heads:
        np.savez(args.out / "output_full_reference.npz", **{name: tensor.numpy() for name, tensor in zip(output_names, split)})
        print("input", tuple(normalized.shape), "outputs", [tuple(t.shape) for t in split])
    else:
        np.save(args.out / "output_reference.npy", split.numpy())
        print("input", tuple(normalized.shape), "output", tuple(split.shape))


if __name__ == "__main__":
    main()

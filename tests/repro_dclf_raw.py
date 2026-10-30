# -*- coding: utf-8 -*-
# Reproduce element4 (7_dclf) raw model output per config_guang3.xml
# Pipeline: cut Cnt=2 overlap=100 -> resize 1024x1024 -> ONNX infer ->
#           conf>=0.25 decode -> class-aware NMS(0.45) -> map to 4096 image coords
import cv2
import numpy as np
import onnxruntime as ort

IMG = r"E:\02_myproject\0_project\proj2_20260411\tests\11\07976_7187500_1782733480999.jpg"
ONNX = r"E:\02_myproject\0_project\proj2_20260411\config\guang3\G3_DCLF_4_20261024_best.onnx"

# element4 nodes (from config_guang3.xml)
NODES = [
    dict(name="道床裂缝",   flawID=20, partID=1200, w=50, h=10, fp=50,  mergeBox=1),
    dict(name="电连接线",   flawID=1,  partID=1401, w=30, h=30, fp=100, mergeBox=0),
    dict(name="结构缝",     flawID=1,  partID=1401, w=30, h=30, fp=100, mergeBox=0),
    dict(name="水沟区域",   flawID=1,  partID=1401, w=30, h=30, fp=50,  mergeBox=0),
]
CNT, OVERLAP, COMBINE_ITH, SG_AREA = 2, 100, 100, 90
CONF_TH, NMS_TH, MAX_OUT = 0.25, 0.45, 30
IW = IH = 4096

def cut_bands(img):
    """cut_batchsize_img for Cnt=2 overlap=100 on full image"""
    r = (0, 0, img.shape[1], img.shape[0])
    ihh = r[3] // CNT
    bands = []
    for i in range(CNT):
        x, y, w, h = r[0], r[1] + ihh * i, r[2], ihh + OVERLAP
        if y + h > img.shape[0] - 1:
            y = max(0, img.shape[0] - 1 - h)
        bands.append((x, y, w, h))
    return bands

def infer_band(sess, band_img):
    """resize->blob->infer->decode->NMS, returns [(x,y,w,h,conf,cls)] in band(1024) space"""
    img = cv2.resize(band_img, (1024, 1024))
    blob = img[:, :, ::-1].astype(np.float32) / 255.0   # BGR->RGB, /255
    blob = np.transpose(blob, (2, 0, 1))[None]          # NCHW
    out = sess.run(None, {sess.get_inputs()[0].name: blob})[0]
    out = np.squeeze(out)
    if out.shape[0] < out.shape[1]:                     # [4+nc, N] transposed
        out = out.T                                     # -> [N, 4+nc]
    boxes, scores, cls_ids = [], [], []
    for row in out:
        cls = int(np.argmax(row[4:]))
        sc = float(row[4 + cls])
        if sc < CONF_TH:
            continue
        cx, cy, w, h = row[:4]
        x0 = max(0.0, min(float(cx - w / 2), 1024.0))
        y0 = max(0.0, min(float(cy - h / 2), 1024.0))
        x1 = max(0.0, min(float(cx + w / 2), 1024.0))
        y1 = max(0.0, min(float(cy + h / 2), 1024.0))
        boxes.append([x0, y0, x1 - x0, y1 - y0])
        scores.append(sc)
        cls_ids.append(cls)
    # class-aware NMS
    keep = []
    for c in sorted(set(cls_ids)):
        idx = [i for i, k in enumerate(cls_ids) if k == c]
        sub = cv2.dnn.NMSBoxes([boxes[i] for i in idx], [scores[i] for i in idx], CONF_TH, NMS_TH)
        keep += [idx[i] for i in np.array(sub).flatten()]
    keep.sort(key=lambda i: -scores[i])
    return [(boxes[i][0], boxes[i][1], boxes[i][2], boxes[i][3], scores[i], cls_ids[i]) for i in keep[:MAX_OUT]]

def combine_vecf(flaws, iTH, W, H):
    """merge same-list boxes whose iTH-expanded rects intersect (mergeBox=1 nodes)"""
    flaws = [list(f) for f in flaws]
    i = 0
    while i < len(flaws):
        o1 = flaws[i]
        r1 = [max(int(o1[0]) - iTH, 0), max(int(o1[1]) - iTH, 0), int(o1[2]) + 2 * iTH, int(o1[3]) + 2 * iTH]
        r1[2] = min(W - r1[0], r1[2]); r1[3] = min(H - r1[1], r1[3])
        j = i + 1
        while j < len(flaws):
            o2 = flaws[j]
            r2 = [max(int(o2[0]) - iTH, 0), max(int(o2[1]) - iTH, 0), int(o2[2]) + 2 * iTH, int(o2[3]) + 2 * iTH]
            r2[2] = min(W - r2[0], r2[2]); r2[3] = min(H - r2[1], r2[3])
            if min(r1[1]+r1[3], r2[1]+r2[3]) >= max(r1[1], r2[1]) and min(r1[0]+r1[2], r2[0]+r2[2]) >= max(r1[0], r2[0]):
                nx = max(0, min(int(o1[0]), int(o2[0]))); ny = max(0, min(int(o1[1]), int(o2[1])))
                nr = max(int(o1[0]+o1[2]), int(o2[0]+o2[2])); nb = max(int(o1[1]+o1[3]), int(o2[1]+o2[3]))
                o1[4] = max(o1[4], o2[4])
                o1[0], o1[1], o1[2], o1[3] = nx, ny, min(W-nx, nr-nx), min(H-ny, nb-ny)
                r1 = [max(int(o1[0]) - iTH, 0), max(int(o1[1]) - iTH, 0), int(o1[2]) + 2 * iTH, int(o1[3]) + 2 * iTH]
                r1[2] = min(W - r1[0], r1[2]); r1[3] = min(H - r1[1], r1[3])
                flaws.pop(j)
            else:
                j += 1
        i += 1
    return flaws

def main():
    img = cv2.imread(IMG)
    assert img is not None and img.shape[:2] == (IH, IW)
    sess = ort.InferenceSession(ONNX, providers=["CPUExecutionProvider"])
    print("onnx out shape:", sess.get_outputs()[0].shape)

    bands = cut_bands(img)
    print("bands:", bands)

    all_flaws, all_areas = [], []   # (x,y,w,h,conf,cls) in 4096-image space
    for bi, (bx, by, bw, bh) in enumerate(bands):
        dets = infer_band(sess, img[by:by+bh, bx:bx+bw])
        print(f"\n=== band{bi} rect=({bx},{by},{bw},{bh})  resize->1024x1024 ===")
        print(f"raw detections: {len(dets)}")
        fx, fy = bw / 1024.0, bh / 1024.0
        for d in dets:
            x, y, w, h, conf, cls = d
            nd = NODES[cls] if cls < len(NODES) else dict(name=f"cls{cls}", flawID=-1, partID=-1)
            gx, gy, gw, gh = x * fx + bx, y * fy + by, w * fx, h * fy
            print(f"  cls={cls} {nd['name']:<6} conf={conf:.3f}  model1024: x={x:7.1f} y={y:7.1f} w={w:6.1f} h={h:6.1f}"
                  f"  -> img4096: x={gx:7.1f} y={gy:7.1f} w={gw:6.1f} h={gh:6.1f}")
            if nd["flawID"] in (1, 2):
                if nd["partID"] in (1201, 1301, 1401):
                    all_areas.append([gx, gy, gw, gh, conf, 1401])
            else:
                all_flaws.append([gx, gy, gw, gh, conf, cls])

    # combine(): per-class mergeBox + w/h/fp filter
    print("\n=== after delete_noflaw + combine(iTH=%d) + w/h/fp filter ===" % COMBINE_ITH)
    outs = []
    for cid, nd in enumerate(NODES):
        sel = [f for f in all_flaws if f[5] == cid]
        if nd["mergeBox"] == 1 and sel:
            sel = combine_vecf(sel, COMBINE_ITH, IW - 1, IH - 1)
        for f in sel:
            if f[2] < nd["w"] or f[3] < nd["h"] or int(f[4] * 100) < nd["fp"]:
                print(f"  FILTERED cls={cid} {nd['name']} conf={f[4]:.3f} box=({f[0]:.0f},{f[1]:.0f},{f[2]:.0f},{f[3]:.0f})")
                continue
            outs.append((f, nd))
            print(f"  keep cls={cid} {nd['name']} conf={f[4]:.3f} box=({f[0]:.1f},{f[1]:.1f},{f[2]:.1f},{f[3]:.1f})")
    print(f"  area boxes (partID1401, sg_cnt={len(all_areas)}):")
    for a in all_areas:
        print(f"    area1401 conf={a[4]:.3f} box=({a[0]:.1f},{a[1]:.1f},{a[2]:.1f},{a[3]:.1f})")

    # fjmn_lf_js_process: crack boxes covered >= SG_AREA% by 1401 areas -> drop
    print("\n=== fjmn_sg water-gutter cover check (th=%d%%) ===" % SG_AREA)
    kept = []
    for f, nd in outs:
        if nd["partID"] == 1200 and nd["flawID"] in (20, 31, 22):
            fx, fy, fw, fh = [int(v) for v in f[:4]]
            mask = np.zeros((max(fh, 1), max(fw, 1)), np.uint8)
            for a in all_areas:
                ax, ay, aw, ah = [int(v) for v in a[:4]]
                ix, iy = max(fx, ax), max(fy, ay)
                ix2, iy2 = min(fx + fw, ax + aw), min(fy + fh, ay + ah)
                if ix2 > ix and iy2 > iy:
                    mask[iy - fy:iy2 - fy, ix - fx:ix2 - fx] = 255
            cover = cv2.countNonZero(mask) * 100.0 / (fw * fh) if fw * fh > 0 else 0
            drop = cover >= SG_AREA
            print(f"  flawID={nd['flawID']} cover={cover:.1f}% th={SG_AREA}% -> {'DROP' if drop else 'keep'}")
            if drop:
                continue
        kept.append((f, nd))

    # XLBH type merge: same type keeps max conf
    print("\n=== final (same XLBH_type -> max conf) ===")
    best = {}
    for f, nd in kept:
        t = "1024" if nd["flawID"] == 20 else "XLBH-000"
        if t not in best or f[4] > best[t][0][4]:
            best[t] = (f, nd)
    for t, (f, nd) in best.items():
        print(f"  OUT type={t} {nd['name']} conf={f[4]:.3f} "
              f"xmin={int(f[0])} ymin={int(f[1])} xmax={int(f[0]+f[2])} ymax={int(f[1]+f[3])}")

if __name__ == "__main__":
    main()

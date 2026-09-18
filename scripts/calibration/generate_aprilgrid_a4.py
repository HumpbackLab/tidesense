#!/usr/bin/env python3
"""Generate TideSense's fixed A4 Kalibr target. Requires PyMuPDF (fitz).

Tag codes and bit orientation follow Kalibr's kalibr_create_target_pdf:
https://github.com/ethz-asl/kalibr/blob/1f60227442d25e36365ef5f72cd80b9666d73467/aslam_offline_calibration/kalibr/python/kalibr_create_target_pdf
Fetched generator SHA256: 37b7a6c837faea2593652fc3df9979c1ef4eae6d19262a829fb6d77ccf6b76c5
Kalibr defaults: t36h11, rotation=2, borderBits=2, symmetric corner squares.
"""
from pathlib import Path
import fitz

CODES = [57401312644, 58383764297, 59366215950, 61331119256, 63296022562, 65260925868, 1453707397, 4401062356, 9313320621, 10295772274, 14225578886, 17172933845, 18155385498, 19137837151, 21102740457, 22085192110, 24050095416, 27979902028, 28962353681, 33874611946, 34857063599, 35839515252, 37804418558, 42716676823]
COLS, ROWS, SIZE, GAP = 6, 4, 32.0, 9.6  # millimetres
MM = 72 / 25.4
STEM = "tidesense-aprilgrid-a4-6x4-32mm"


def generate(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    doc = fitz.open()
    page = doc.new_page(width=297 * MM, height=210 * MM)
    shape = page.new_shape()

    def rect(x, y, w, h):
        # Pattern coordinates have y pointing up, as in Kalibr's PyX generator.
        shape.draw_rect(fitz.Rect(x * MM, (210-y-h) * MM,
                                  (x+w) * MM, (210-y) * MM))

    width = COLS * SIZE + (COLS-1) * GAP
    height = ROWS * SIZE + (ROWS-1) * GAP
    ox, oy = (297-width)/2, (210-height)/2
    cell = SIZE / 10  # 6 data cells, two black border cells on each side
    for tag_id, code in enumerate(CODES):
        x = ox + (tag_id % COLS) * (SIZE+GAP)
        y = oy + (tag_id // COLS) * (SIZE+GAP)
        rect(x, y, SIZE, 2*cell)
        rect(x, y+8*cell, SIZE, 2*cell)
        rect(x, y+2*cell, 2*cell, 6*cell)
        rect(x+8*cell, y+2*cell, 2*cell, 6*cell)
        for row in range(6):
            for col in range(6):
                # Equivalent to Kalibr's np.rot90(codeMatrix, 2).
                bit = (5-row)*6 + (5-col)
                if not code & (1 << bit):
                    rect(x+(col+2)*cell, y+(7-row)*cell, cell, cell)
        for dx in (-GAP, SIZE):
            for dy in (-GAP, SIZE):
                rect(x+dx, y+dy, GAP, GAP)
    shape.finish(color=None, fill=(0, 0, 0))
    shape.commit()
    page.insert_text((18.9*MM, 9*MM),
        "TideSense | Kalibr AprilGrid | 6 x 4 | tag36h11 | tag 32 mm | gap 9.6 mm",
        fontsize=9)
    page.insert_text((18.9*MM, 201*MM),
        "A4 landscape - print at 100% / Actual size - no Fit to page", fontsize=8)
    page.draw_line((175*MM, 200*MM), (275*MM, 200*MM), width=0.5)
    for x in (175, 275):
        page.draw_line((x*MM, 198.5*MM), (x*MM, 201.5*MM), width=0.5)
    page.insert_text((212*MM, 197.5*MM), "100 mm", fontsize=8)
    doc.set_metadata({"title": "TideSense A4 Kalibr AprilGrid 6x4 32mm",
                      "author": "TideSense", "subject": "Print at 100%; tagSpacing=0.3"})
    doc.save(directory / (STEM + ".pdf"), deflate=True)
    page.get_pixmap(dpi=150).save(directory / (STEM + "-preview.png"))
    (directory / (STEM + ".yaml")).write_text(
        "# A4 landscape, 100% printing; measure printed black tag edges.\n"
        "# Kalibr tag36h11, 2-cell black border; IDs 0..23.\n"
        "# ID 0 bottom-left; IDs increase left-to-right, then bottom-to-top.\n"
        "target_type: aprilgrid\ntagCols: 6\ntagRows: 4\n"
        "tagSize: 0.032\ntagSpacing: 0.3\n")
    print(directory / (STEM + ".pdf"))


if __name__ == "__main__":
    generate(Path(__file__).resolve().parent)

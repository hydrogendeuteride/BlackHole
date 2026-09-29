"""50-second insert after '온도에 따라 색을 입혔습니다'.

Planck spectra and the renderer's CIE-derived blackbody LUT share temperature.
Spectra are peak-normalized; displayed RGB is exposure-normalized for color comparison.
LUT provenance / CC BY-SA 4.0 attribution: tools/data/README.md.
"""
from pathlib import Path
import re

from manim import *

config.background_color = "#0B1018"
GOLD, INK = "#FFD166", "#8193A8"
ROOT = Path(__file__).resolve().parents[1]
LUT = np.array([
    [float(value) for value in row.split(",")]
    for row in re.findall(r"vec4\(([-\d.,\s]+)\)",
                          (ROOT / "shaders/blackhole/blackbody.glsl").read_text())
])


def thermal_color(temperature):
    index = np.log(np.clip(temperature, 500, 1e6)/500) * 511/np.log(2000)
    lo = min(int(index), 510)
    rgb = interpolate(LUT[lo, :3], LUT[lo+1, :3], index-lo)
    rgb = np.clip(rgb / rgb.max(), 0, 1)
    srgb = np.where(rgb <= 0.0031308, 12.92*rgb, 1.055*rgb**(1/2.4)-0.055)
    return rgb_to_color(srgb)


def spectrum(wavelength_nm, temperature):
    wavelength = np.asarray(wavelength_nm)*1e-9
    return wavelength**-5 / np.expm1(0.014387768775039337/(wavelength*temperature))


class Blackbody(Scene):
    def at(self, time, *animations, duration=1):
        if time > self.time + 1e-6:
            self.wait(time-self.time)
        if animations:
            self.play(*animations, run_time=duration)

    def construct(self):
        def text(value, size=23, color=WHITE):
            return Text(value, font="Noto Sans CJK KR", font_size=size, color=color)

        title = text("온도와 색", 30).move_to([0, 3.45, 0])
        temperature = ValueTracker(3000)
        bar_left, bar_width, bar_y = -5.8, 11.6, -1.9
        minimum, maximum = 1000, 12000

        def bar_x(value):
            return bar_left + bar_width*(value-minimum)/(maximum-minimum)

        bar = VGroup(*(
            Rectangle(width=bar_width/240+0.025, height=0.48, stroke_width=0,
                      fill_color=thermal_color(minimum+(i+0.5)*(maximum-minimum)/240),
                      fill_opacity=1).move_to([bar_left+(i+0.5)*bar_width/240, bar_y, 0])
            for i in range(240)
        ))
        outline = Rectangle(width=bar_width, height=0.48, stroke_color=INK,
                            stroke_width=1).move_to([0, bar_y, 0])
        bar_title = text("온도별 색 (K)", 23).move_to([-4.8, -1.2, 0])
        bar_note = text("밝기 정규화", 17, INK).move_to([4.85, -1.2, 0])
        ticks = VGroup()
        for value in (1000, 3000, 6500, 12000):
            tick = Line([bar_x(value), -2.17, 0], [bar_x(value), -2.27, 0], color=INK)
            caption = text(f"{value:,}", 19, INK).next_to(tick, DOWN, buff=0.1)
            ticks.add(tick, caption)
        pointer = Triangle(color=WHITE, fill_opacity=1, stroke_width=0).scale(0.09).rotate(PI)
        pointer.add_updater(lambda mob: mob.move_to([bar_x(temperature.get_value()), -1.53, 0]))

        panel = RoundedRectangle(width=2.95, height=2.55, corner_radius=0.12,
                                 stroke_color="#29384C", stroke_width=1,
                                 fill_color="#101927", fill_opacity=1).move_to([4.6, 1.1, 0])
        temperature_label = text("온도", 20, INK).move_to([4.6, 2.06, 0])
        swatch = RoundedRectangle(width=2.15, height=1.02, corner_radius=0.08,
                                  stroke_color=INK, stroke_width=1, fill_opacity=1)
        swatch.move_to([4.6, 0.55, 0])
        swatch.add_updater(lambda mob: mob.set_fill(thermal_color(temperature.get_value())))
        temperature_number = DecimalNumber(3000, num_decimal_places=0, group_with_commas=True,
                                            mob_class=Text, font_size=33, color=GOLD)
        temperature_number.add_updater(lambda mob: mob.set_value(temperature.get_value())
                                       .move_to([4.4, 1.55, 0]))
        kelvin = text("K", 25, GOLD).move_to([5.47, 1.55, 0])
        self.add(title, bar, outline, bar_title, bar_note, ticks, pointer,
                 panel, temperature_label, swatch, temperature_number, kelvin)

        axes = Axes(x_range=[250, 2000, 250], y_range=[0, 1.1, 0.5], x_length=8.6,
                    y_length=2.8, tips=False,
                    axis_config={"color": INK, "stroke_width": 1.5, "include_ticks": False})
        axes.move_to([-1.45, 0.85, 0])
        axis_labels = VGroup()
        for wavelength in (400, 700, 1000, 2000):
            point = axes.c2p(wavelength, 0)
            axis_labels.add(text(str(wavelength), 17, INK).next_to(point, DOWN, buff=0.13))
        axis_labels.add(text("파장 (nm)", 19, INK).next_to(axes, DOWN, buff=0.4))
        axis_labels.add(text("상대 세기 (최댓값 1)", 18, INK)
                        .move_to([0, 2.93, 0]).align_to(axes, LEFT))
        band_left, band_right = axes.c2p(380, 0), axes.c2p(780, 1.06)
        visible = Rectangle(width=band_right[0]-band_left[0], height=band_right[1]-band_left[1],
                            stroke_width=0, fill_color=GOLD, fill_opacity=0.07)
        visible.move_to((band_left+band_right)/2)
        visible_label = text("가시광선", 20, GOLD).move_to(axes.c2p(580, 1.1)+UP*0.2)
        infrared_label = text("적외선", 18, INK).move_to(axes.c2p(1550, 1.03))
        wavelengths = np.linspace(250, 2000, 480)

        def spectral_curve():
            value = temperature.get_value()
            peak_nm = 2.897771955e6/value
            values = spectrum(wavelengths, value)/spectrum(peak_nm, value)
            points = np.array([axes.c2p(wavelength, strength)
                               for wavelength, strength in zip(wavelengths, values)])
            return VMobject(stroke_color=thermal_color(value), stroke_width=3.5).set_points_as_corners(points)

        curve = always_redraw(spectral_curve)
        peak = always_redraw(lambda: Dot(axes.c2p(2.897771955e6/temperature.get_value(), 1),
                                        radius=0.045, color=WHITE))
        graph = VGroup(axes, axis_labels, visible, visible_label, infrared_label)
        self.at(6, FadeIn(graph), FadeIn(curve), FadeIn(peak), duration=1.5)
        self.at(12, Transform(title, text("흑체복사", 30)
                              .move_to(title)), duration=1)
        self.at(18, temperature.animate.set_value(6000), duration=6)
        self.at(24, temperature.animate.set_value(10000), duration=6)
        self.at(30, visible.animate.set_fill(opacity=0.18),
                swatch.animate.set_stroke(GOLD, 2.5), duration=1)
        self.at(35, temperature.animate.set_value(3000), duration=3)
        self.at(38, temperature.animate.set_value(6500), duration=2)
        self.at(40, FadeOut(graph), FadeOut(curve), FadeOut(peak),
                FadeOut(swatch), FadeOut(panel), FadeOut(temperature_label),
                FadeOut(temperature_number), FadeOut(kelvin),
                Transform(title, text("원반의 온도", 30).move_to(title)), duration=1)
        for mob in (curve, peak, swatch, temperature_number):
            mob.clear_updaters()

        # Illustrative top view using the renderer's radial temperature law.
        disk_center = np.array([-2.7, 0.85, 0])
        disk = VGroup()
        inner, outer = 0.58, 1.65
        for i in range(100):
            r = inner+(outer-inner)*i/100
            next_r = inner+(outer-inner)*(i+1)/100
            value = 6500*(inner/((r+next_r)/2))**0.75
            disk.add(Annulus(inner_radius=r, outer_radius=next_r+0.012, stroke_width=0,
                             fill_color=thermal_color(value), fill_opacity=1).move_to(disk_center))
        disk_note = text("위에서 본 모형", 19, INK)
        disk_note.move_to([-2.7, 2.82, 0])
        inner_label = text("안쪽  6,500 K", 26, thermal_color(6500)).move_to([2.95, 1.5, 0])
        outer_temp = 6500*(inner/outer)**0.75
        outer_label = text(f"바깥쪽  약 {round(outer_temp/100)*100:,} K", 26,
                           thermal_color(outer_temp)).move_to([3.1, 0.35, 0])
        inner_line = Line(disk_center+RIGHT*(inner+0.1), [1.2, 1.5, 0], color=INK, stroke_width=1.5)
        outer_line = Line(disk_center+RIGHT*(outer-0.05)+DOWN*0.35, [1.2, 0.35, 0], color=INK, stroke_width=1.5)
        self.at(41, FadeIn(disk), FadeIn(disk_note), FadeIn(inner_label), FadeIn(outer_label),
                Create(inner_line), Create(outer_line), duration=1.5)
        self.at(45, temperature.animate.set_value(outer_temp), duration=2)
        self.at(48, temperature.animate.set_value(6500), duration=1)
        self.at(50)

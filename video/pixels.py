from manim import *

config.background_color = "#0B1018"
GOLD, INK, OBJECT, BACKGROUND = "#FFD166", "#8193A8", "#56B6CA", "#25364C"


class Pixels(ThreeDScene):
    def at(self, time, *animations, duration=1):
        if time - self.time > 1e-6:
            self.wait(time - self.time)
        if animations:
            self.play(*animations, run_time=duration)

    def construct(self):
        origin, center, radius = np.array([0, 0, 5]), np.array([0.7, 0, -4]), 2
        grid = VGroup(*(Square(side_length=0.3, stroke_color=INK, stroke_width=0.8,
                               fill_color=config.background_color, fill_opacity=1)
                        .move_to([(c-9.5)*0.3, (5.5-r)*0.3, 0]) for r in range(12) for c in range(20)))

        def trace(cell):
            direction = normalize(cell.get_center()-origin)
            offset = origin-center
            b = np.dot(direction, offset)
            discriminant = b*b-np.dot(offset, offset)+radius*radius
            distance = -b-np.sqrt(discriminant) if discriminant >= 0 else 14
            return origin+distance*direction, discriminant >= 0

        hits = [trace(cell) for cell in grid]
        hit_index = 110
        miss_index = 104
        target, _ = hits[hit_index]
        selection = grid[hit_index].copy().set_fill(opacity=0).set_stroke(GOLD, 3)
        object = Sphere(center=center, radius=radius, resolution=(20, 32), checkerboard_colors=[OBJECT, OBJECT],
                        fill_color=OBJECT, fill_opacity=1, stroke_width=0)
        eye = Dot3D(origin, radius=0.1, color=WHITE)
        pixel = Square(side_length=0.8, stroke_color=GOLD, stroke_width=3,
                       fill_color=config.background_color, fill_opacity=1).move_to([-5.5, 2.4, 0])
        label = Text("선택한 픽셀", font="Noto Sans CJK KR", font_size=23, color=WHITE).next_to(pixel, UP, buff=0.2)
        detail = VGroup(pixel, label)
        self.set_camera_orientation(phi=0, theta=-90*DEGREES, zoom=1.4)
        self.at(0, Create(grid), duration=2)
        self.at(5, LaggedStart(*(Indicate(grid[i], color=GOLD, scale_factor=1) for i in [39, 40, 41, 42, 43]), lag_ratio=0.3), duration=3)
        self.at(8.5, LaggedStart(*(cell.animate.set_fill(BACKGROUND, 0.3) for cell in grid), lag_ratio=0.02), duration=3)
        self.at(13.5, Create(selection), duration=1)
        self.at(16.5)
        self.move_camera(zoom=2.1, frame_center=grid[hit_index].get_center(), run_time=2)
        self.at(22)
        self.move_camera(zoom=0.78, frame_center=ORIGIN, run_time=0.7)
        self.move_camera(phi=40*DEGREES, theta=35*DEGREES, gamma=118*DEGREES,
                         run_time=1.3,
                         added_anims=[grid.animate.set_fill(opacity=0.06), FadeIn(object), FadeIn(eye)])
        self.add_fixed_in_frame_mobjects(detail)
        guide = Line3D(target, origin, thickness=0.009, color=INK)
        photon = Dot3D(target, radius=0.075, color=GOLD)
        self.add(guide, photon)
        self.play(MoveAlongPath(photon, Line(target, origin), rate_func=linear), run_time=2)
        self.play(FadeOut(photon), run_time=0.4)
        reverse = Arrow3D(origin, origin+normalize(target-origin)*2, color=GOLD, thickness=0.012, height=0.18, base_radius=0.06)
        self.at(27, FadeIn(reverse), duration=1)
        ray = Line(origin, target, color=GOLD, stroke_width=3)
        self.at(32, Create(ray, rate_func=linear), duration=4)
        mark = Dot3D(target, radius=0.08, color=GOLD)
        self.at(37, FadeIn(mark), duration=1)
        self.at(40, grid[hit_index].animate.set_fill(OBJECT, 1), pixel.animate.set_fill(OBJECT, 1), duration=1)
        self.play(Indicate(selection, color=GOLD, scale_factor=1.7), run_time=0.7)
        self.at(44, FadeOut(ray), FadeOut(mark), FadeOut(reverse), FadeOut(guide), duration=0.7)
        self.at(45, Transform(selection, grid[miss_index].copy().set_fill(opacity=0).set_stroke(GOLD, 3)),
                pixel.animate.set_fill(config.background_color, 1), duration=1)
        miss = Line(origin, hits[miss_index][0], color=GOLD, stroke_width=3)
        self.at(48, Create(miss, rate_func=linear), duration=2.5)
        self.at(51, grid[miss_index].animate.set_fill(BACKGROUND, 1), pixel.animate.set_fill(BACKGROUND, 1), duration=1)
        self.play(Indicate(selection, color=GOLD, scale_factor=1.7), run_time=0.7)
        self.at(54, FadeOut(miss), FadeOut(selection), FadeOut(detail), duration=0.7)
        self.at(55, LaggedStart(*(cell.animate.set_fill(OBJECT if hit else BACKGROUND, 1)
                                 for cell, (_, hit) in zip(grid, hits)), lag_ratio=0.02), duration=3)
        self.at(58)
        self.move_camera(phi=0, theta=-90*DEGREES, gamma=0, frame_center=ORIGIN,
                         run_time=1, added_anims=[FadeOut(object), FadeOut(eye)])
        self.move_camera(zoom=1.4, run_time=0.5)
        self.at(61)

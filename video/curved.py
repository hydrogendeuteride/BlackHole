from pathlib import Path
from tempfile import TemporaryDirectory
import xml.etree.ElementTree as ET

from manim import *
from matplotlib import rc_context
from matplotlib.mathtext import math_to_image
from scipy.integrate import solve_ivp

config.background_color = "#0B1018"
GOLD, INK, OBJECT, BACKGROUND = "#FFD166", "#8193A8", "#56B6CA", "#25364C"


def equation(source, width):
    with TemporaryDirectory() as directory:
        svg = Path(directory) / "equation.svg"
        with rc_context({"savefig.transparent": True, "mathtext.fontset": "cm"}):
            math_to_image("$" + source + "$", svg, format="svg", color="white", dpi=160)
        tree = ET.parse(svg)
        for parent in tree.iter():
            for child in list(parent):
                if child.get("id") == "patch_1":
                    parent.remove(child)
        tree.write(svg)
        return SVGMobject(svg).set_width(width)


def ray_points(start, direction, center, radius, reach=18):
    radial = normalize(start - center)
    tangent = normalize(direction - np.dot(direction, radial) * radial)
    u = radius / np.linalg.norm(start - center)
    slope = -u * np.dot(direction, radial) / np.dot(direction, tangent)

    def horizon(angle, state):
        return state[0] - 1

    def escape(angle, state):
        return state[0] - radius / reach

    horizon.terminal = escape.terminal = True
    escape.direction = -1
    solution = solve_ivp(lambda angle, state: [state[1], -state[0] + 1.5 * state[0]**2],
                         [0, 16], [u, slope], events=[horizon, escape],
                         rtol=1e-9, atol=1e-11, max_step=0.005, dense_output=True)
    angles = np.linspace(0, solution.t[-1], 3000)
    points = center + (radius / solution.sol(angles)[0])[:, None] * (
        np.cos(angles)[:, None] * radial + np.sin(angles)[:, None] * tangent)
    return points


def path_from(points, color=GOLD, width=3):
    return VMobject(stroke_color=color, stroke_width=width).set_points_as_corners(points)


def resample(points, count):
    distances = np.r_[0, np.cumsum(np.linalg.norm(np.diff(points, axis=0), axis=1))]
    samples = np.linspace(0, distances[-1], count)
    return np.column_stack([np.interp(samples, distances, points[:, i]) for i in range(3)])


class Curved(ThreeDScene):
    # Times match 02_light_paths.srt; this clip begins at 01:01.
    def at(self, time, *animations, duration=1):
        if time - 61 > self.time + 1e-6:
            self.wait(time - 61 - self.time)
        if animations:
            self.play(*animations, run_time=duration)

    def construct(self):
        eye_pos = np.array([0., 0, 5])
        sphere_pos = np.array([0.7, 0, -4])
        grid = VGroup(*(Square(side_length=0.3, stroke_color=INK, stroke_width=0.8,
                               fill_opacity=1).move_to([(c-9.5)*0.3, (5.5-r)*0.3, 0])
                        for r in range(12) for c in range(20)))
        for cell in grid:
            direction = normalize(cell.get_center() - eye_pos)
            offset = eye_pos - sphere_pos
            b = np.dot(direction, offset)
            cell.set_fill(OBJECT if b*b - np.dot(offset, offset) + 4 >= 0 else BACKGROUND)
        selected = grid[110]
        start = selected.get_center()
        direction = normalize(start - eye_pos)
        target = eye_pos + 14 * direction
        eye = Dot3D(eye_pos, radius=0.1, color=WHITE)
        selection = selected.copy().set_fill(opacity=0).set_stroke(GOLD, 3)
        straight = DashedLine(eye_pos, target, color=INK, stroke_width=2, dash_length=0.1)
        hole_pos = np.array([1.7, -0.3, -5.0])
        hole_radius = 0.55
        surface = Sphere(center=hole_pos, radius=hole_radius, resolution=(20, 32),
                         checkerboard_colors=[BLACK, BLACK], stroke_width=0, fill_opacity=1)
        view = np.array([np.sin(40*DEGREES)*np.cos(35*DEGREES),
                         np.sin(40*DEGREES)*np.sin(35*DEGREES), np.cos(40*DEGREES)])
        toward_view = self.camera.get_focal_distance()*view - hole_pos
        view_distance = np.linalg.norm(toward_view)
        normal = normalize(toward_view)
        rim = Circle(radius=hole_radius*np.sqrt(1-(hole_radius/view_distance)**2),
                     stroke_color=INK, stroke_width=1.8)
        rim.rotate(np.arccos(normal[2]), axis=np.cross(OUT, normal))
        rim.move_to(hole_pos + normal*hole_radius**2/view_distance).set_shade_in_3d(True)
        hole = VGroup(surface, rim)
        self.camera.should_apply_shading = False
        grid.set_shade_in_3d(True)
        selection.set_shade_in_3d(True)
        straight.set_shade_in_3d(True)
        curved_points = ray_points(start, direction, hole_pos, hole_radius)
        outside = np.flatnonzero(np.linalg.norm(curved_points, axis=1) > 10)
        if len(outside):
            curved_points = curved_points[:outside[0]+1]
        curved_points = resample(np.vstack([eye_pos, curved_points]), 181)
        photon_path = path_from(curved_points)
        curved = VGroup(*(Line(a, b, color=GOLD, stroke_width=3).set_shade_in_3d(True)
                          for a, b in zip(curved_points, curved_points[1:])))
        detail_pixel = Square(side_length=0.8, stroke_color=GOLD, stroke_width=3,
                              fill_color=OBJECT, fill_opacity=1).move_to([-5.5, 2.4, 0])
        detail = VGroup(detail_pixel, Text("선택한 픽셀", font="Noto Sans CJK KR", font_size=23)
                        .next_to(detail_pixel, UP, buff=0.2))
        self.set_camera_orientation(phi=0, theta=-90*DEGREES, zoom=1.4)
        self.add(grid)
        self.move_camera(zoom=0.78, run_time=0.7)
        self.move_camera(phi=40*DEGREES, theta=35*DEGREES, gamma=118*DEGREES, run_time=1.3,
                         added_anims=[grid.animate.set_fill(opacity=0.06), FadeIn(eye)])
        self.add_fixed_in_frame_mobjects(detail)
        self.play(FadeIn(selection), Create(straight), run_time=0.6)
        self.at(64, FadeIn(hole), duration=1)
        self.at(66, LaggedStart(*(Create(segment, rate_func=linear) for segment in curved),
                               lag_ratio=1, rate_func=linear), duration=3.5)
        photon = Dot3D(eye_pos, radius=0.075, color=GOLD)
        self.at(70, FadeIn(photon), duration=0.2)
        self.play(MoveAlongPath(photon, photon_path, rate_func=linear), run_time=3.3)
        self.play(FadeOut(photon), run_time=0.3)
        self.at(74, selected.animate.set_fill(BACKGROUND, 1),
                detail_pixel.animate.set_fill(BACKGROUND, 1), duration=1)
        self.at(79, *(FadeOut(mob) for mob in [grid, eye, selection, straight, hole, curved, detail]), duration=0.8)
        self.set_camera_orientation(phi=0, theta=-90*DEGREES, gamma=0, zoom=1, frame_center=ORIGIN)

        center = np.array([-0.6, -0.75, 0])
        radius = 0.55
        horizon = Circle(radius=radius, stroke_color=INK, stroke_width=2,
                         fill_color=BLACK, fill_opacity=1).move_to(center).set_z_index(3)

        def planar(height):
            points = ray_points(np.array([-6., height, 0]), RIGHT, center, radius)
            outside = np.flatnonzero((abs(points[:, 0]) > 6.4) | (points[:, 1] < -3.1) | (points[:, 1] > 2.1))
            if len(outside):
                points = points[:outside[0]+1]
            return resample(points, 220)

        points = planar(0.79)
        path = path_from(points)
        ghost = path.copy().set_stroke(INK, 1.5, opacity=0.4)
        self.play(FadeIn(horizon), Create(ghost), run_time=1.5)
        general = equation(r"\frac{d^2x^\mu}{d\lambda^2}+\Gamma^\mu_{\alpha\beta}\frac{dx^\alpha}{d\lambda}\frac{dx^\beta}{d\lambda}=0", 7).move_to([0, 2.35, 0])
        title = Text("지오데식 방정식", font="Noto Sans CJK KR", font_size=25, color=INK).to_edge(UP, buff=0.25)
        self.at(83, FadeIn(general), FadeIn(title), duration=1)
        paper = Rectangle(width=6, height=3.2, stroke_color=INK,
                          fill_color="#102535", fill_opacity=1).shift(DOWN*0.2)
        paper_line = Line(LEFT*2.4+DOWN*0.2, RIGHT*2.4+DOWN*0.2, color=GOLD)
        paper_dot = Dot(paper_line.get_start(), color=WHITE)
        self.at(88, FadeOut(general), FadeOut(horizon), FadeOut(ghost),
                FadeIn(paper), duration=0.8)
        self.at(91, Create(paper_line), FadeIn(paper_dot), duration=1)
        self.play(MoveAlongPath(paper_dot, paper_line, rate_func=linear), run_time=4)
        self.at(99, FadeOut(paper), FadeOut(paper_line), FadeOut(paper_dot), duration=0.5)
        globe_center = np.array([0., -0.3, 0])
        globe_radius = 2.1
        tilt = rotation_matrix(18*DEGREES, RIGHT)

        def surface_point(latitude, longitude):
            return np.array([np.cos(latitude)*np.sin(longitude), np.sin(latitude),
                             np.cos(latitude)*np.cos(longitude)])

        def project_globe(points):
            projected = np.asarray(points) @ tilt.T
            projected[:, 2] = 0
            return globe_center + globe_radius*projected

        graticule = VGroup()
        lines = [np.array([surface_point(latitude, longitude) for longitude in np.linspace(-PI, PI, 181)])
                 for latitude in np.linspace(-PI/3, PI/3, 5)]
        lines += [np.array([surface_point(latitude, longitude) for latitude in np.linspace(-PI/2, PI/2, 121)])
                  for longitude in np.linspace(-PI, PI, 12, endpoint=False)]
        for points_on_globe in lines:
            visible = np.flatnonzero((points_on_globe @ tilt.T)[:, 2] >= 0)
            for run in np.split(visible, np.flatnonzero(np.diff(visible) > 1)+1):
                if len(run) > 1:
                    graticule.add(path_from(project_globe(points_on_globe[run]), color="#46617A", width=1))
        disk = Circle(radius=globe_radius, stroke_color=INK, stroke_width=2,
                      fill_color="#102535", fill_opacity=1).move_to(globe_center)
        a = surface_point(20*DEGREES, -60*DEGREES)
        b = surface_point(35*DEGREES, 60*DEGREES)
        angle = np.arccos(np.dot(a, b))
        fractions = np.linspace(0, 1, 160)
        great_circle = (np.sin((1-fractions)*angle)[:, None]*a
                        + np.sin(fractions*angle)[:, None]*b) / np.sin(angle)
        route = path_from(project_globe(great_circle), width=4)
        endpoints = project_globe([a, b])
        pins = VGroup(*(Dot(point, radius=0.065, color=GOLD) for point in endpoints))
        labels = VGroup(Text("A", font_size=24).next_to(pins[0], LEFT, buff=0.15),
                        Text("B", font_size=24).next_to(pins[1], RIGHT, buff=0.15))
        globe = VGroup(disk, graticule, pins, labels)
        globe_title = Text("지구 표면 위의 최단 경로", font="Noto Sans CJK KR", font_size=25, color=INK).move_to(title)
        self.at(100, FadeIn(globe), Transform(title, globe_title), duration=0.6)
        self.at(107, Create(route, rate_func=linear), duration=3)
        traveller = Dot(endpoints[0], radius=0.09, color=WHITE)
        self.at(119, FadeIn(traveller), duration=0.2)
        self.play(MoveAlongPath(traveller, route, rate_func=linear), run_time=3.3)
        self.play(FadeOut(traveller), run_time=0.3)
        # Compare the short great-circle arc with a longer surface route.
        via = surface_point(-20*DEGREES, 0)
        def globe_arc(start, end):
            angle = np.arccos(np.dot(start, end))
            f = np.linspace(0, 1, 80)
            return (np.sin((1-f)*angle)[:, None]*start + np.sin(f*angle)[:, None]*end)/np.sin(angle)
        detour = path_from(project_globe(np.vstack([globe_arc(a, via), globe_arc(via, b)])),
                           color=INK, width=2)
        short_label = Text("표면을 따라 두 점을 잇는 가장 짧은 길", font="Noto Sans CJK KR",
                           font_size=23, color=GOLD).move_to([0, -2.85, 0])
        self.at(126, Create(detour), FadeIn(short_label), duration=2)
        self.at(134, FadeOut(detour), Indicate(route, color=GOLD), duration=1.5)
        self.at(144, FadeOut(short_label), duration=0.5)
        geodesic_title = Text("지오데식 방정식", font="Noto Sans CJK KR", font_size=25, color=INK).move_to(title)
        self.at(145, FadeOut(globe), FadeOut(route), FadeIn(horizon), FadeIn(ghost),
                FadeIn(general), Transform(title, geodesic_title), duration=0.6)
        self.play(Create(path, rate_func=linear), run_time=3.4)
        general_key = VGroup(
            Text("x : 시공간의 위치    λ : 경로를 따라가는 매개변수", font="Noto Sans CJK KR", font_size=22),
            Text("Γ : 시공간의 기하가 경로에 반영되는 항", font="Noto Sans CJK KR", font_size=22)
        ).arrange(DOWN, buff=0.2).move_to([0, 1.1, 0])
        self.at(155, FadeIn(general_key), duration=1)
        self.at(167, FadeOut(general_key), duration=0.5)

        def heading(text):
            return Text(text, font="Noto Sans CJK KR", font_size=25, color=INK).move_to(title)

        self.at(168, FadeOut(general), FadeOut(path),
                Transform(title, heading("회전하지 않는 블랙홀")), duration=1)
        self.at(176, Transform(title, heading("빛의 경로가 놓인 평면")),
                ghost.animate.set_stroke(opacity=0.7), duration=1)
        self.at(184, Transform(title, heading("시계 방향 + 중심에서의 거리")), duration=1)
        clock_face = Circle(radius=1.05, stroke_color=INK).move_to([3.7, 0, 0])
        clock_numbers = VGroup(*(Text(str(n), font_size=20, color=INK).move_to(
            clock_face.get_center()+0.8*np.array([np.sin(n*PI/6), np.cos(n*PI/6), 0]))
            for n in (3, 6, 9, 12)))
        clock_hand = Arrow(clock_face.get_center(), clock_face.get_center()+RIGHT*0.75,
                           buff=0, color=GOLD)
        clock_group = VGroup(clock_face, clock_numbers, clock_hand)
        self.play(FadeIn(clock_group), run_time=1)
        self.at(189, Rotate(clock_hand, angle=PI/3, about_point=clock_face.get_center()), duration=2)
        self.at(193, FadeOut(clock_group), Transform(title, heading("거리와 각도로 나타낸 위치")), duration=0.5)

        distances = np.linalg.norm(points-center, axis=1)
        nearest = int(np.argmin(distances))
        indices = np.arange(nearest+1)
        far_index = np.interp(4*radius, distances[:nearest+1][::-1], indices[::-1])
        close_index = np.interp(2*radius, distances[:nearest+1][::-1], indices[::-1])
        progress = ValueTracker(far_index)

        def position():
            value = np.clip(progress.get_value(), 0, len(points)-1)
            i = min(int(value), len(points)-2)
            return interpolate(points[i], points[i+1], value-i)

        marker = always_redraw(lambda: Dot(position(), color=GOLD, radius=0.065).set_z_index(5))
        radial = always_redraw(lambda: Line(center, position(), color=GOLD, stroke_width=2))
        r_label = always_redraw(lambda: Text("r", font_size=28, color=GOLD).set_z_index(6)
                                .move_to(center + 0.8*(position()-center)
                                         + 0.32*normalize(np.cross(OUT, position()-center))))
        self.play(FadeIn(marker), FadeIn(radial), run_time=1)
        self.at(195, FadeIn(r_label), duration=0.7)
        baseline = DashedLine(center, center+RIGHT*2.5, color=INK, stroke_width=1.5)
        baseline_label = Text("기준 방향", font="Noto Sans CJK KR", font_size=19, color=INK)
        baseline_label.next_to(baseline, DOWN, buff=0.15).align_to(baseline, RIGHT)

        def polar_angle():
            offset = position()-center
            return np.arctan2(offset[1], offset[0])

        arc = always_redraw(lambda: Arc(radius=0.72, start_angle=0, angle=polar_angle(),
                                        arc_center=center, color=GOLD, stroke_width=2))
        phi_label = always_redraw(lambda: Text("φ", font_size=25, color=GOLD).set_z_index(6)
                                  .move_to(center+0.96*np.array([np.cos(polar_angle()/2),
                                                                np.sin(polar_angle()/2), 0])))
        self.at(202, Create(baseline), FadeIn(baseline_label), FadeIn(arc), FadeIn(phi_label), duration=1)
        self.play(progress.animate.set_value(far_index+10), run_time=3, rate_func=linear)
        rs_line = Line(center, center+DOWN*radius, color=INK, stroke_width=2).set_z_index(4)
        rs_label = Text("rₛ", font_size=22, color=INK).next_to(rs_line, LEFT, buff=0.12).set_z_index(4)
        self.at(213, FadeIn(rs_line), FadeIn(rs_label),
                Indicate(horizon, color=GOLD, scale_factor=1), duration=1)
        definition = equation(r"u=\frac{r_s}{r}", 1.6).move_to([0, 2.35, 0])
        self.at(224, FadeIn(definition), Transform(title, heading("거리를 u로 바꾸어 표현")),
                progress.animate.set_value(far_index), duration=1)

        r_value_label = equation(r"r/r_s=", 0.95).move_to([3.1, 0.65, 0])
        r_value = DecimalNumber(4, num_decimal_places=2, mob_class=Text, font_size=27, color=GOLD)
        r_value.next_to(r_value_label, RIGHT, buff=0.15)
        r_value.add_updater(lambda mob: mob.set_value(np.linalg.norm(position()-center)/radius))
        u_value_label = equation(r"u=", 0.55).move_to([3.3, -0.05, 0])
        u_value = DecimalNumber(0.25, num_decimal_places=2, mob_class=Text, font_size=27, color=GOLD)
        u_value.next_to(u_value_label, RIGHT, buff=0.15)
        u_value.add_updater(lambda mob: mob.set_value(radius/np.linalg.norm(position()-center)))
        values = VGroup(r_value_label, r_value, u_value_label, u_value)
        example = equation(r"r=4r_s\quad\Rightarrow\quad u=\frac{1}{4}", 3.3).move_to([3.6, -1.85, 0])
        near_example = equation(r"r=2r_s\quad\Rightarrow\quad u=\frac{1}{2}", 3.3).move_to(example)
        self.at(232, FadeIn(values), FadeIn(example), duration=0.7)
        self.at(240, FadeOut(example), duration=0.2)
        self.play(progress.animate.set_value(close_index), run_time=2.5)
        example.become(near_example)
        self.play(FadeIn(example), run_time=0.5)
        trend = equation(r"r\downarrow\qquad u\uparrow", 2).move_to(example)
        self.at(248, Transform(example, trend), duration=0.8)

        unit_note = Text("같은 거리를 다른 눈금으로 표시", font="Noto Sans CJK KR",
                         font_size=22, color=GOLD).move_to([3.6, -2.45, 0])
        self.at(254, FadeIn(unit_note), duration=1)
        self.at(262, FadeOut(unit_note), duration=0.5)
        lhs = equation(r"\frac{d^2u}{d\phi^2}", 1.0)
        rhs = equation(r"=-u+\frac{3}{2}u^2", 3.0)
        simple = VGroup(lhs, rhs).arrange(RIGHT, buff=0.15).move_to([-1.5, 2.35, 0])
        self.at(263, FadeIn(simple), definition.animate.set_width(1.4).move_to([2.5, 2.35, 0]),
                FadeOut(example), Transform(title, heading("회전하지 않는 블랙홀의 빛 경로")), duration=1.5)
        # Everyday derivative analogy, explicitly separate from the angular ray equation.
        analogy_title = Text("변화율을 읽는 예: 자동차", font="Noto Sans CJK KR", font_size=23, color=INK)
        analogy_rows = VGroup(*(
            Text(line, font="Noto Sans CJK KR", font_size=25, color=color)
            for line, color in [("위치", WHITE), ("위치의 변화율 → 속도", GOLD),
                                ("속도의 변화율 → 가속도", OBJECT)]
        )).arrange(DOWN, aligned_edge=LEFT, buff=0.45)
        analogy = VGroup(analogy_title, analogy_rows).arrange(DOWN, buff=0.45).move_to([0, -0.1, 0])
        geometry = [marker, radial, r_label, values, rs_line, rs_label,
                    baseline, baseline_label, arc, phi_label, ghost, horizon]
        self.at(272, *(FadeOut(mob) for mob in geometry), FadeIn(analogy_title), duration=1)
        self.at(276, FadeIn(analogy_rows[0]), duration=0.7)
        self.at(281, FadeIn(analogy_rows[1]), duration=0.7)
        self.at(286, FadeIn(analogy_rows[2]), duration=0.7)
        self.at(291, FadeOut(analogy), duration=0.5)
        meaning = Text("u의 변화율이 달라지는 정도", font="Noto Sans CJK KR", font_size=21, color=GOLD)
        meaning.move_to([-1.5, 1.45, 0])
        self.at(292, lhs.animate.set_color(GOLD), FadeIn(meaning), duration=1)
        derivative_keys = VGroup(
            equation(r"u\quad\longrightarrow\quad\frac{du}{d\phi}\quad\longrightarrow\quad\frac{d^2u}{d\phi^2}", 6.5),
            Text("현재 값       각도에 따른 변화율       그 변화율의 변화", font="Noto Sans CJK KR", font_size=23),
            Text("이 식의 기준은 시간 t가 아니라 각도 φ", font="Noto Sans CJK KR", font_size=23, color=GOLD)
        ).arrange(DOWN, buff=0.4).move_to([0, -0.3, 0])
        self.at(296, FadeIn(derivative_keys), duration=1)
        self.at(306, FadeOut(derivative_keys), duration=0.5)
        self.at(306.5, *(FadeIn(mob) for mob in geometry), duration=0.5)
        next_step = Text("현재 u와 변화율 → 다음 위치", font="Noto Sans CJK KR", font_size=21, color=GOLD).move_to(meaning)
        self.at(307, lhs.animate.set_color(WHITE), rhs.animate.set_color(GOLD),
                Transform(meaning, next_step), duration=1)
        self.at(315, *(FadeOut(mob) for mob in [marker, radial, r_label, values, rs_line, rs_label,
                                              baseline, baseline_label, arc, phi_label, ghost, meaning]),
                rhs.animate.set_color(WHITE), duration=0.8)
        for mob in [marker, radial, r_label, r_value, u_value, arc, phi_label]:
            mob.clear_updaters()

        nodes = resample(points, 28)
        segments = VGroup(*(Line(a, b, color=GOLD, stroke_width=3) for a, b in zip(nodes, nodes[1:])))
        coarse = segments.copy().set_stroke(INK, 1.5, opacity=0.35)
        self.play(LaggedStart(*(Create(segment) for segment in coarse), lag_ratio=0.08), run_time=2.5)
        dot = Dot(nodes[0], color=GOLD, radius=0.065)
        arrow = Arrow(nodes[0], nodes[0]+normalize(nodes[1]-nodes[0])*0.65,
                      buff=0, color=GOLD, stroke_width=3, max_tip_length_to_length_ratio=0.2)
        self.at(320, FadeIn(dot), GrowArrow(arrow), duration=0.7)
        self.at(325)
        for i in range(3):
            next_arrow = Arrow(nodes[i+1], nodes[i+1]+normalize(nodes[i+2]-nodes[i+1])*0.65,
                               buff=0, color=GOLD, stroke_width=3, max_tip_length_to_length_ratio=0.2)
            self.play(Create(segments[i]), dot.animate.move_to(nodes[i+1]),
                      Transform(arrow, next_arrow), run_time=1.2, rate_func=linear)
        self.at(337, FadeOut(arrow), FadeOut(dot),
                LaggedStart(*(Create(segment) for segment in segments[3:]), lag_ratio=0.1), duration=3)
        self.at(341, FadeOut(coarse), FadeOut(segments), FadeIn(path), duration=0.5)
        self.at(346, FadeOut(path), FadeOut(horizon), FadeOut(simple), FadeOut(definition),
                FadeOut(title), duration=0.7)
        shader = (Path(__file__).resolve().parents[1] / "shaders/blackhole/ray.glsl").read_text()
        begin = shader.index("float acceleration")
        code = Code(code_string=shader[begin:].strip(), language="glsl", formatter_style="monokai",
                    add_line_numbers=True, line_numbers_from=3, background="rectangle",
                    background_config={"fill_color": "#111B29", "stroke_color": "#33445A",
                                       "stroke_width": 1, "buff": 0.35},
                    paragraph_config={"font": "DejaVu Sans Mono", "font_size": 25,
                                      "line_spacing": 0.5})
        code.scale(min(12.5/code.width, 5.35/code.height)).move_to([0, 0.2, 0])
        source_label = Text("shaders/blackhole/ray.glsl", font="DejaVu Sans Mono", font_size=20, color=INK)
        source_label.next_to(code, UP, buff=0.2).align_to(code, LEFT)
        update_note = Text("u와 변화율을 함께 갱신 · dt는 각도 간격", font="Noto Sans CJK KR", font_size=23, color=GOLD)
        update_note.move_to([0, -2.8, 0])
        self.at(347, FadeIn(code), FadeIn(source_label), duration=1)
        highlight = SurroundingRectangle(code.code_lines[2], color=GOLD, buff=0.12,
                                         stroke_width=2)
        self.at(354, Create(highlight),
                *(line.animate.set_opacity(0.65) for i, line in enumerate(code.code_lines) if i != 2),
                duration=1)
        update_box = SurroundingRectangle(VGroup(code.code_lines[11], code.code_lines[12]),
                                          color=GOLD, buff=0.12, stroke_width=2)
        self.at(360, Transform(highlight, update_box), FadeIn(update_note),
                *(line.animate.set_opacity(1 if i in (11, 12) else 0.65)
                  for i, line in enumerate(code.code_lines)), duration=1)
        self.at(369, FadeOut(code), FadeOut(source_label), FadeOut(update_note), FadeOut(highlight),
                FadeIn(horizon), FadeIn(path), duration=0.7)
        far = path_from(planar(1.8), color=OBJECT)
        capture = path_from(planar(0.45), color=WHITE)
        title.become(heading("블랙홀에서의 거리와 빛의 경로"))
        self.at(370, FadeIn(title), duration=0.5)
        self.play(Create(far, rate_func=linear), path.animate.set_stroke(width=4), run_time=3)
        self.at(376, Create(capture, rate_func=linear), duration=3)
        self.at(384, FadeOut(title), duration=1)
        self.at(390)

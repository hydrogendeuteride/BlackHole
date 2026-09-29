from manim import *
from scipy.integrate import solve_bvp

config.background_color = "#0B1018"
GOLD, INK, DISK = "#FFD166", "#8193A8", "#56B6CA"


def disk_ray():
    # Schwarzschild ray in the disk's side view, from camera to far-side source.
    radius = 0.55
    angles = np.linspace(0, PI, 250)
    ends = radius / np.array([5.0, 3.5])
    guess = np.linspace(*ends, len(angles)) + 0.2*np.sin(angles)
    solution = solve_bvp(
        lambda angle, state: np.vstack([state[1], -state[0] + 1.5*state[0]**2]),
        lambda start, end: np.array([start[0] - ends[0], end[0] - ends[1]]),
        angles, np.vstack([guess, np.gradient(guess, angles)]),
        tol=1e-8, max_nodes=10000)
    if not solution.success:
        raise RuntimeError(solution.message)
    angles = np.linspace(0, PI, 900)
    distances = radius / solution.sol(angles)[0]
    points = distances[:, None] * np.column_stack([
        np.cos(angles), np.sin(angles), np.zeros(len(angles))])
    return points[::-1]


class DiskPaths(Scene):
    # Insert at 00:35 in 04_disk.srt; return to the render at 00:55.
    def at(self, time, *animations, duration=1):
        if time > self.time + 1e-6:
            self.wait(time - self.time)
        if animations:
            self.play(*animations, run_time=duration)

    def construct(self):
        center = DOWN*0.7
        points = disk_ray() + center
        source_pos, camera_pos = points[0], points[-1]

        def label(text, size=23, color=INK):
            return Text(text, font="Noto Sans CJK KR", font_size=size, color=color)

        title = label("원반을 옆에서 본 단면", 26).to_edge(UP, buff=0.35)
        horizon = Circle(radius=0.55, stroke_color=INK, stroke_width=2,
                         fill_color=BLACK, fill_opacity=1).move_to(center)
        disk = VGroup()
        for side in (-1, 1):
            for i in range(9):
                tile = Rectangle(width=0.28, height=0.12, stroke_width=0,
                                 fill_color=DISK if i % 2 == 0 else "#25364C",
                                 fill_opacity=1)
                tile.move_to(center + RIGHT*side*(1.8 + (i+0.5)*0.28))
                disk.add(tile)
        source = Dot(source_pos, radius=0.075, color=GOLD).set_z_index(3)
        source_label = label("원반 뒤쪽", 22, GOLD).move_to([-4.2, -1.25, 0])
        camera = Dot(camera_pos, radius=0.09, color=WHITE)
        camera_label = label("카메라", 22, WHITE).next_to(camera, RIGHT, buff=0.2)
        self.add(title, disk, horizon, source, source_label, camera, camera_label)

        upper = VMobject(stroke_color=GOLD, stroke_width=3).set_points_as_corners(points)
        photon = Dot(source_pos, radius=0.085, color=WHITE).set_z_index(4)
        self.at(0.5, Create(upper, rate_func=linear), duration=2)
        self.add(photon)
        self.play(MoveAlongPath(photon, upper, rate_func=linear), run_time=2.5)
        self.play(FadeOut(photon), run_time=0.3)

        backward = normalize(points[-2] - camera_pos)
        apparent = camera_pos + backward*((2.35-camera_pos[1])/backward[1])
        extension = DashedLine(camera_pos, apparent, color=GOLD, stroke_width=2,
                               dash_length=0.12)
        apparent_label = label("이 방향에 있는 것처럼 보임", 23, GOLD)
        apparent_label.next_to(apparent, UP, buff=0.2).align_to(source_label, LEFT)
        self.at(6, Create(extension), duration=1.5)
        self.at(7.5, FadeIn(apparent_label), duration=0.5)

        lower_points = points.copy()
        lower_points[:, 1] = 2*center[1] - lower_points[:, 1]
        lower = VMobject(stroke_color=DISK, stroke_width=3).set_points_as_corners(lower_points)
        lower_photon = Dot(source_pos, radius=0.085, color=WHITE).set_z_index(4)
        self.at(12, extension.animate.set_stroke(opacity=0.35),
                apparent_label.animate.set_opacity(0.35),
                Create(lower, rate_func=linear), duration=1.5)
        self.add(lower_photon)
        self.play(MoveAlongPath(lower_photon, lower, rate_func=linear), run_time=2)
        self.play(FadeOut(lower_photon), run_time=0.3)
        self.at(20)

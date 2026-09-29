"""A standalone 20-second globe shot: two surface paths, one shortest route."""
from manim import *

config.background_color = "#10151F"
SURFACE = "#182A3B"
GRID = "#496075"
GOLD = "#FFD27D"
BLUE = "#78BBCD"


class GeodesicSurface(Scene):
    def construct(self):
        center = np.array([-1.8, -0.35, 0.0])
        radius = 2.45

        def text(value, size=26, color=WHITE):
            return Text(value, font="Noto Sans CJK KR", font_size=size, color=color)

        def point(latitude, longitude):
            return np.array([np.cos(latitude)*np.sin(longitude),
                             np.sin(latitude), np.cos(latitude)*np.cos(longitude)])

        def project(points):
            result = np.asarray(points).copy()
            result[..., 2] = 0
            return center + radius*result

        def line(points, color, width=2):
            return VMobject(stroke_color=color, stroke_width=width, fill_opacity=0).set_points_as_corners(points)

        globe = Circle(radius=radius, stroke_color=GRID, stroke_width=2,
                       fill_color=SURFACE, fill_opacity=1).move_to(center)
        grid = VGroup()
        for lat in np.linspace(-PI/3, PI/3, 7):
            grid.add(line(project([point(lat, lon) for lon in np.linspace(-PI/2, PI/2, 160)]), GRID, 1))
        for lon in np.linspace(-PI/2, PI/2, 9):
            grid.add(line(project([point(lat, lon) for lat in np.linspace(-PI/2, PI/2, 160)]), GRID, 1))
        grid.set_stroke(opacity=0.55)
        title = text("지구 표면을 따라, 두 점을 잇는 길", 30).move_to([0, 3.25, 0])
        a, b = point(30*DEGREES, -70*DEGREES), point(30*DEGREES, 70*DEGREES)
        angle = np.arccos(np.dot(a, b))
        t = np.linspace(0, 1, 240)
        great = (np.sin((1-t)*angle)[:, None]*a + np.sin(t*angle)[:, None]*b)/np.sin(angle)
        alternate = [point(30*DEGREES, lon) for lon in np.linspace(-70*DEGREES, 70*DEGREES, 240)]
        route = line(project(great), GOLD, 5)
        other = line(project(alternate), BLUE, 3)
        pins = VGroup(Dot(project(a), color=WHITE, radius=.065), Dot(project(b), color=WHITE, radius=.065))
        letters = VGroup(text("A", 24).next_to(pins[0], LEFT, buff=.18),
                         text("B", 24).next_to(pins[1], RIGHT, buff=.18))

        self.play(FadeIn(globe), FadeIn(grid), FadeIn(title), run_time=1.2)
        self.play(FadeIn(pins), FadeIn(letters), run_time=.8)
        self.wait(1)
        self.play(Create(other, rate_func=linear), run_time=2)
        other_note = text("위도선을 따라간 길", 24, BLUE).move_to([3.4, 1.3, 0])
        other_length = text(f"{6371*np.cos(30*DEGREES)*140*DEGREES:,.0f} km", 30, BLUE)
        other_length.next_to(other_note, DOWN, buff=.22)
        self.play(FadeIn(other_note), FadeIn(other_length), run_time=1)
        self.play(Create(route, rate_func=linear), run_time=2.5)
        shortest = text("표면 위의 최단 경로", 24, GOLD).move_to([3.4, -.2, 0])
        shortest_length = text(f"{6371*angle:,.0f} km", 30, GOLD).next_to(shortest, DOWN, buff=.22)
        self.play(FadeIn(shortest), FadeIn(shortest_length), run_time=1)
        dot = Dot(project(a), radius=.08, color=WHITE)
        self.add(dot)
        self.play(MoveAlongPath(dot, route, rate_func=linear), other.animate.set_stroke(opacity=.35), run_time=3.5)
        self.play(FadeOut(dot), run_time=.3)
        term = text("지오데식", 32, GOLD).move_to([3.4, -1.65, 0])
        detail = text("대권의 짧은 호", 21, color="#9EACBD").next_to(term, DOWN, buff=.2)
        self.play(FadeIn(term), FadeIn(detail), run_time=.8)
        self.wait(20-self.time)

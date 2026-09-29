from manim import *
from scipy.integrate import quad, solve_ivp

config.background_color = "#080C14"


def trajectory(impact):
    def horizon(t, state):
        return state[0] - 1

    def escape(t, state):
        return state[0] - 0.001

    horizon.terminal = escape.terminal = True
    escape.direction = -1
    initial_angle = PI - quad(lambda u: 1 / np.sqrt(impact**-2 - u*u + u**3), 0, 0.001)[0]
    solution = solve_ivp(lambda t, s: [s[1], -s[0] + 1.5*s[0]**2], [0, 20],
                         [0.001, np.sqrt(impact**-2 - 0.001**2 + 0.001**3)],
                         events=[horizon, escape], rtol=1e-10, atol=1e-12, max_step=0.005, dense_output=True)
    angles = np.linspace(0, solution.t[-1], 24000)
    radius = 1 / solution.sol(angles)[0]
    points = np.column_stack([radius*np.cos(initial_angle-angles), radius*np.sin(initial_angle-angles), np.zeros(len(angles))])
    points = points * (100/135) + DOWN*(50/135)
    inside = np.flatnonzero((abs(points[:, 0]) < 7.4) & (abs(points[:, 1]) < 4.3))
    points = points[max(0, inside[0]-1):inside[-1]+2]
    start = np.flatnonzero(points[:, 0] >= -880/135)[0]
    a, b = points[start-1:start+1]
    points = np.vstack([a + (b-a)*((-880/135-a[0])/(b[0]-a[0])), points[start:]])
    lengths = np.r_[0, np.cumsum(np.linalg.norm(np.diff(points, axis=0), axis=1))]
    samples = np.linspace(0, lengths[-1], 900)
    points = np.column_stack([np.interp(samples, lengths, points[:, i]) for i in range(3)])
    return VMobject(stroke_color="#F4F6FA", stroke_width=3).set_points_as_corners(points), lengths[-1]


class LightPaths(Scene):
    def construct(self):
        paths = [trajectory(b) for b in (3.4, 2.8, 2.2)]
        horizon = Circle(radius=100/135, stroke_color="#748299", stroke_width=2,
                         fill_color=BLACK, fill_opacity=1).shift(DOWN*(50/135)).set_z_index(5)
        self.add(horizon)
        self.wait(0.5)
        speed = max(length for _, length in paths)/5
        self.play(AnimationGroup(*(Create(path, run_time=length/speed, rate_func=linear)
                                   for path, length in paths), lag_ratio=0))
        self.wait(2.5)

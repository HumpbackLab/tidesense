#!/usr/bin/env python3
"""Checks OADEV normalization, noise units, gap handling and fit rejection."""
import unittest
import numpy as np
from allan import overlapping_adev, fit_component, sampling


class AllanTest(unittest.TestCase):
    def test_matches_direct_window_means_without_bridging_gaps(self):
        values = np.array([1., 3., 2., 8., 5., 10., 12., 11., 17., 15., 20.])[:, None]
        segments = [(0, 5), (5, 11)]
        actual, counts, _ = overlapping_adev(values, segments, [1, 2])
        for index, m in enumerate([1, 2]):
            differences = [values[i+m:i+2*m].mean()-values[i:i+m].mean()
                           for a, b in segments for i in range(a, b-2*m+1)]
            self.assertAlmostEqual(actual[index, 0], np.sqrt(np.mean(np.square(differences))/2))
            self.assertEqual(counts[index], len(differences))

    def test_constant_bias_does_not_change_allan(self):
        x = np.random.default_rng(3).normal(size=(1000, 2))
        a, _, _ = overlapping_adev(x, [(0, 1000)], [1, 10, 40])
        b, _, _ = overlapping_adev(x+100., [(0, 1000)], [1, 10, 40])
        np.testing.assert_allclose(a, b, rtol=1e-12)

    def test_white_noise_continuous_density(self):
        dt, density, n = .005, .02, 200000
        x = np.random.default_rng(4).normal(size=(n, 1))*density/np.sqrt(dt)
        adev, _, _ = overlapping_adev(x, [(0, n)], [1, 20, 100])
        np.testing.assert_allclose(adev[:, 0]*np.sqrt(np.array([1, 20, 100])*dt), density, rtol=.07)

    def test_bias_random_walk_continuous_density(self):
        dt, walk, n = .005, .003, 200000
        x = np.cumsum(np.random.default_rng(5).normal(size=(n, 1))*walk*np.sqrt(dt), axis=0)
        adev, _, _ = overlapping_adev(x, [(0, n)], [100, 500, 1000])
        np.testing.assert_allclose(adev[:, 0]*np.sqrt(3/(np.array([100, 500, 1000])*dt)), walk, rtol=.15)

    def test_fits_require_correct_slope(self):
        t = np.geomspace(.02, 100, 100)
        support = np.full(len(t), 100)
        white = fit_component(t, .03/np.sqrt(t), support, 'white')
        self.assertAlmostEqual(white['coefficient'], .03)
        walk = fit_component(t, .004*np.sqrt(t/3), support, 'walk')
        self.assertAlmostEqual(walk['coefficient'], .004)
        self.assertIsNone(fit_component(t, .03/np.sqrt(t), support, 'walk'))
        self.assertIsNone(fit_component(t, np.ones(len(t)), support, 'walk'))
        self.assertIsNone(fit_component(t, .004*np.sqrt(t/3), np.ones(len(t)), 'walk'))

    def test_timestamp_gap_creates_separate_segments(self):
        s = np.arange(100, dtype=np.int64)*5_000_000
        s[50:] += 10_000_000
        dt, _, segments = sampling(s)
        self.assertAlmostEqual(dt, .005)
        self.assertEqual(segments, [(0, 50), (50, 100)])


if __name__ == '__main__':
    unittest.main()

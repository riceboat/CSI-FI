import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
from scipy.ndimage import gaussian_filter1d
from numpy.linalg import norm
from scipy.signal import ShortTimeFFT
from scipy.signal.windows import gaussian
import math
from scipy.signal import butter, filtfilt
fig, axes = plt.subplots(nrows=2, ncols=2)


class TimeSeries:
    def __init__(self, time, data):
        self.time = time
        self.data = data

    def quantize_time(self):
        quantized_ns = np.array([])
        self.time = self.time.reset_index(drop=True)
        for i in range(len(self.time)):
            quantized_ns = np.append(
                quantized_ns, round(self.time[i].value, -8))
        i = 0
        while i < quantized_ns.size - 1:
            if (quantized_ns[i] == quantized_ns[i + 1]):
                quantized_ns = np.delete(quantized_ns, i + 1)
                self.data = np.delete(self.data, i + 1)
            else:
                i += 1
        self.time = pd.to_datetime(quantized_ns, unit="ns").to_series()

    def time_density(self):
        dt = np.array([])
        self.time = self.time.reset_index(drop=True)
        for i in range(1, len(self.time)):
            # time delay in ms
            dt = np.append(
                dt, int(self.time[i].value - self.time[i - 1].value) / 1000000)
        dt = TimeSeries(self.time[:-1], dt)
        # dt.gaussian(3)
        dt.plot_data_vs_time(sx=1)

        return dt

    def remove_outliers(self, i=3):
        start = self.data.size
        z = np.abs(stats.zscore(self.data))
        outliers = np.where(z > i)[0]
        self.time = self.time.reset_index(drop=True)
        for i in range(len(outliers) - 1):
            self.data[outliers[i]] = (
                self.data[outliers[i] - 1] + self.data[outliers[i] - 2]) / 2
        # print("Removed " + str(round((outliers.size / start) * 100)) + "%")
        # self.data = self.data.drop(outliers).to_numpy().flatten()
        # self.time = self.time.drop(outliers)

    def moving_average(self, w):
        self.data = np.convolve(self.data, np.ones(w), 'same') / w
        # self.time = self.time[w - 1:self.time.size]

    def plot_data_vs_time(self, data_label="", sx=0, sy=0, color=""):
        if color == "":
            axes[sy, sx].plot(np.array(self.time).flatten(), np.array(self.data),
                              label=data_label, linestyle='solid',
                              marker=".", markersize=2.0, linewidth=1)
        else:
            axes[sy, sx].plot(np.array(self.time).flatten(), np.array(self.data),
                              label=data_label, linestyle='solid',
                              marker=".", markersize=4.0, linewidth=1, color=color)

    def plot_data(self, data_label="", sx=0, sy=0):
        axes[sy, sx].plot(self.data,
                          label=data_label, linestyle='solid',
                          marker=".", markersize=2.0, linewidth=0.5)

    def scale(self):
        self.data = self.data - self.data.mean()
        self.scale_std()

    def normalize(self):
        self.data = (self.data - np.min(self.data)) / np.ptp(self.data)
        self.data += 0.1

    def plot_spectrum(self, sx=1, sy=1):
        dt = 0.01
        fhat = np.fft.fft(self.data, self.data.size)
        PSD = fhat * np.conj(fhat) / self.data.size  # Power spectral density
        # vector of all frequencies
        freq = (1 / (dt * self.data.size)) * np.arange(self.data.size)
        L = np.arange(1, np.floor(self.data.size / 2), dtype='int')
        axes[sx, sy].plot(freq[L], PSD[L],
                          label="Spectrum", linestyle='solid',
                          marker=".", markersize=4.0, linewidth=0.5)

    def gaussian(self, a):
        self.data = gaussian_filter1d(self.data, sigma=a)

    def fft_denoise(self):
        fhat = np.fft.fft(self.data, self.data.size)
        PSD = fhat * np.conj(fhat) / self.data.size  # Power spectral density
        L = np.arange(1, np.floor(self.data.size / 2), dtype='int')
        i = PSD > PSD[L].mean().real
        # i = PSD > a
        PSD = PSD * i
        fhat = fhat * i
        self.data = np.fft.ifft(fhat)

    def butter_bandpass(self, lowcut, highcut, fs, order=1):
        nyquist = 0.5 * fs
        low = lowcut / nyquist
        high = highcut / nyquist
        b, a = butter(order, [low, high], btype='band')
        return b, a

    def butter_bandpass_filter(self, lowcut, highcut, fs, order=1):
        b, a = self.butter_bandpass(lowcut, highcut, fs, order=order)
        filtered_data = filtfilt(b, a, self.data)
        self.data = filtered_data

    def diff(self):
        self.data = np.diff(self.data)
        self.time = self.time[2:]

    def scale_std(self):
        if (np.std(self.data) != 0):
            self.data = self.data / np.std(self.data)

    def autocorr(self):
        result = np.correlate(self.data, self.data, mode='full')
        # first element in ACF is dirac function
        self.data = result[result.size // 2:]

    def power_response(self):
        self.data = self.data ** 2

    def stft(self):
        g_std = 10000  # standard deviation for Gaussian window in samples
        w_n = 100
        w = gaussian(w_n, std=g_std, sym=True)  # symmetric Gaussian window
        SFT = ShortTimeFFT(w, hop=1, fs=100, mfft=w_n, scale_to='psd')
        Sx = SFT.stft(self.data)  # perform the STFT
        # axes[1, 0].imshow(abs(Sx), origin='lower', aspect='auto',
        #                   extent=SFT.extent(self.data.size), cmap='viridis')
        Sx = np.max(abs(Sx), axis=0)
        Sx = TimeSeries(
            self.time[1:], Sx[math.ceil(w_n / 2):-math.floor(w_n / 2)])
        return Sx

    def interp(self, a):
        self.data = np.interp(np.linspace(self.data.min(), self.data.max(),
                                          self.data.size * a),
                              np.linspace(self.data.min(), self.data.max(),
                                          self.data.size), self.data)
        time_ns = np.array([])
        self.time = self.time.reset_index(drop=True)
        for i in range(len(self.time)):
            time_ns = np.append(time_ns, self.time[i].value)
        time_ns = np.interp(np.linspace(time_ns.min(), time_ns.max(),
                                        time_ns.size * a),
                            np.linspace(time_ns.min(), time_ns.max(),
                                        time_ns.size), time_ns)
        self.time = pd.to_datetime(time_ns, unit="ns").to_series()
        if self.time.size != self.data.size:
            print("ERROR")

    def add_timewise(self, other):
        for i in range(min(len(self.data), len(other.data))):
            self.data[i] = min(self.data[i], other.data[i])


class SubCarrier:
    def __init__(self, index, data, time, rssi):
        self.data = data
        self.time = time
        self.index = index
        self.rssi = rssi
        self.real = np.take(self.data, 0, axis=1).flatten()
        self.im = np.take(self.data, 1, axis=1).flatten()
        amp = np.sqrt(self.real**2 + self.im**2)
        self.amplitude = TimeSeries(self.time[:-1], amp[:-1])
        self.phase = TimeSeries(
            self.time[:-1], np.arctan2(self.real, self.im)[:-1])

    def raw_amplitude(self):
        amp = np.sqrt(self.real**2 + self.im**2)
        return TimeSeries(self.time[:-1], amp[:-1])

    def raw_phase(self):
        return TimeSeries(self.time[:-1], np.arctan2(self.real, self.im)[:-1])

    def get_rssi(self):
        return self.rssi

    def conj_mult(self):
        self.data = self.data * np.conjugate(self.data)
        self.real = self.data.real
        self.im = self.data.imag


class CSI:
    def __init__(self, vArray, time, rssi, id):
        self.vArray = vArray
        self.subCarriers = []
        self.time = time
        self.rssi = TimeSeries(time, rssi)
        self.id = id
        for i in range(vArray.shape[0]):
            newS = SubCarrier(i, vArray[i], self.time, self.rssi)
            if np.mean(newS.amplitude.data) > 1:
                newS.amplitude.interp(1)
                newS.time = newS.amplitude.time
                newS.amplitude.remove_outliers(3)
                newS.amplitude.butter_bandpass_filter(0.0001, 1, 100, 1)
                newS.amplitude.normalize()
                # newS.amplitude.plot_data_vs_time(self.id, sy=1, sx=1)
                self.subCarriers.append(newS)

    def amplitude(self):
        amplitude = self.subCarriers[0].amplitude
        ampArray = np.array([])
        for i in range(len(self.subCarriers)):
            sub = self.subCarriers[i]
            amplitude = sub.amplitude
            if ampArray.size == 0:
                ampArray = amplitude.data
            else:
                ampArray = np.vstack((ampArray, amplitude.data))
        return ampArray

    def phase(self):
        phase = self.subCarriers[0].phase()
        # phase.remove_outliers()
        # phase.gaussian(7)
        # phase.scale()
        phaseArray = phase.data
        for i in range(1, len(self.subCarriers)):
            sub = self.subCarriers[i]
            phase = sub.phase()
            # phase.gaussian(7)
            # phase.scale()
            phaseArray = np.vstack((phaseArray, phase.data))
        return phaseArray

    def carrier_diff_std(self):
        std = np.std(self.phase(), axis=0)
        std = TimeSeries(self.time[1:], std)

    def carrier_diff_cosine(self):
        ampArray = self.amplitude()
        cosine_diff = np.array([])
        dt = 1
        for i in range(dt, ampArray.shape[1] - 1):
            tn = np.take(ampArray, i, axis=1)
            tn1 = np.take(ampArray, i - dt, axis=1)
            cosine = 1 - (np.dot(tn, tn1) / (norm(tn) * norm(tn1)))
            cosine_diff = np.append(cosine_diff, cosine)
        cosine_diff = TimeSeries(self.subCarriers[0].time[dt:-1], cosine_diff)
        cosine_diff.plot_data_vs_time(sx=0, sy=0)
        return cosine_diff

    def plot2d(self):
        a = self.amplitude()
        axes[0, 1].imshow(a, cmap='hot',
                          interpolation='nearest', aspect='auto')

    def threshold(self, val):
        thresh = TimeSeries(self.time, np.ones(self.time.size) * val)
        thresh.plot_data_vs_time(sx=0, sy=1)

    def avg(self):
        ampArray = self.amplitude()
        average = TimeSeries(self.time[1:], np.average(ampArray, axis=0))
        if self.id == 1:
            average.plot_data_vs_time(sy=1, color="blue")
        elif self.id == 2:
            average.plot_data_vs_time(sy=1, color="orange")
        elif self.id == 3:
            average.plot_data_vs_time(sy=1, color="green")

    def plot_rssi(self):
        self.rssi.plot_data_vs_time(sx=1, sy=0)


def csv_data_to_csi(data, id):
    timestamp = data.iloc[:, 1:].reading_time / 1000
    timestamp = pd.to_datetime(timestamp, unit="s").reset_index(drop=True)
    rssi = data1.iloc[:, 1:].rssi.reset_index(drop=True)
    buffer = data.iloc[:, 1:].buffer.reset_index(drop=True)
    buffer_len = data.iloc[:, 1:].buffer_len.reset_index(drop=True)
    vArray = np.zeros(shape=(np.max(buffer_len), buffer.size, 2))
    for i in range(len(buffer) - 1):
        sp = buffer[i].split(" ")
        for p in range(0, min(buffer_len[i], len(sp)) - 1, 2):
            vArray[p][i][0] = sp[p]  # real
            vArray[p][i][1] = sp[p + 1]  # imaginary
    # first four bytes can be incorrect due to hardware limitations
    vArray = vArray[4:]
    return CSI(vArray, timestamp, rssi, id)


tot = None
data = pd.read_csv('http://192.168.0.85/csi-data.php')
# data.to_csv('last.csv', index=False)
# data = pd.read_csv('last.csv')
for i in range(1, 4):
    data1 = data.loc[data['esp_id'] == i]
    dt = pd.to_datetime(
        data1.iloc[:, 1:].reading_time / 1000, unit="s")
    csi = csv_data_to_csi(data1, i)
    csi.threshold(5e-6)
    csi.plot_rssi()
    print(i)
    if tot is None:
        tot = csi.carrier_diff_cosine()
    else:
        tot.add_timewise(csi.carrier_diff_cosine())
tot.plot_data_vs_time(sx=0, sy=1)

plt.show()

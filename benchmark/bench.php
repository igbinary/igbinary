<?php

class Bench {
	private $name;
	private $headerWritten = false;

	private $started = false;

	private $startTime;
	private $stopTime;

	private $startUsage;
	private $stopUsage;

	private $iterations;

	public function __construct($name) {
		$this->name = $name;
	}

	private function getResourceUsage() {
		$rusage = getrusage();
		$time = $rusage['ru_utime.tv_sec'] * 1000000 + $rusage['ru_utime.tv_usec'];
		$time += $rusage['ru_stime.tv_sec'] * 1000000 + $rusage['ru_stime.tv_usec'];

		return $time;
	}

	public function start() {
		if ($this->started) {
			throw new RuntimeException("Already started.");
		}
		$this->startTime = microtime(true);
		$this->stopTime = $this->startTime;

		$rusage = getrusage();

		$this->startUsage = $this->getResourceUsage();
		$this->stopUsage = $this->startUsage;
		$this->started = true;
	}

	public function stop($i = 1) {
		if (!$this->started) {
			throw new RuntimeException("Not started.");
		}

		$this->stopTime = microtime(true);
		$this->stopUsage = $this->getResourceUsage();

		$this->iterations = (int)$i;
		$this->started = false;
	}

	public function writeHeader() {
		$header = implode("\t", array(
			'name', 'start time', 'iterations', 'duration', 'rusage',
		));
		echo $header, "\n";
		$this->headerWritten = true;
	}

	public function write() {
		if ($this->started) {
			$this->stop();
		}

		if (!$this->headerWritten) {
			$this->writeHeader();
		}

		printf("%s\t%.6f\t%d\t%.8f\t%.6f\n",
			$this->name,
			$this->startTime,
			$this->iterations,
			$this->stopTime - $this->startTime,
			$this->stopUsage - $this->startUsage);
	}
}



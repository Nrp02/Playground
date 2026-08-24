import multiprocessing as mp

import worker
from queue_types import Job, JobResult, JobStatus


class Scheduler:
    def __init__(self, num_workers: int = 4):
        self.num_workers = num_workers
        self.job_queue = mp.Queue()
        self.result_queue = mp.Queue()
        self._next_id = 0
        self._processes: list[mp.Process] = []

    def submit(self, func_name: str, *args, max_retries: int = 3, **kwargs) -> int:
        job_id = self._next_id
        self._next_id += 1
        self.job_queue.put(Job(job_id, func_name, args, kwargs, max_retries=max_retries))
        return job_id

    def start(self):
        for i in range(self.num_workers):
            p = mp.Process(target=worker.run, args=(i, self.job_queue, self.result_queue))
            p.start()
            self._processes.append(p)

    def run_to_completion(self, expected_jobs: int) -> dict[int, JobResult]:
        self.start()

        final: dict[int, JobResult] = {}
        history: dict[int, list[JobResult]] = {}

        while len(final) < expected_jobs:
            res: JobResult = self.result_queue.get()
            history.setdefault(res.job_id, []).append(res)
            if res.status in (JobStatus.DONE, JobStatus.FAILED):
                final[res.job_id] = res

        self.job_queue.put(worker.SENTINEL)
        for p in self._processes:
            p.join(timeout=2)

        self.history = history
        return final

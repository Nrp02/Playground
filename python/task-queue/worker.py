import time

from queue_types import Job, JobResult, JobStatus, REGISTRY

SENTINEL = None


def run(worker_id: int, job_queue, result_queue):
    while True:
        job: Job = job_queue.get()
        if job is SENTINEL:
            job_queue.put(SENTINEL)
            break

        job.attempt += 1
        result_queue.put(JobResult(job.job_id, JobStatus.RUNNING, attempts=job.attempt))

        fn = REGISTRY.get(job.func_name)
        try:
            if fn is None:
                raise KeyError(f"unregistered job function: {job.func_name}")
            value = fn(*job.args, attempt=job.attempt, **job.kwargs)
            result_queue.put(JobResult(job.job_id, JobStatus.DONE, result=value, attempts=job.attempt))
        except Exception as exc:
            error_text = f"{exc.__class__.__name__}: {exc}"
            if job.attempt < job.max_retries:
                backoff = 0.1 * (2 ** (job.attempt - 1))
                result_queue.put(
                    JobResult(job.job_id, JobStatus.PENDING, error=error_text, attempts=job.attempt)
                )
                time.sleep(backoff)
                job_queue.put(job)
            else:
                result_queue.put(
                    JobResult(job.job_id, JobStatus.FAILED, error=error_text, attempts=job.attempt)
                )

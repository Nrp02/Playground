from queue_types import JobStatus, register
from scheduler import Scheduler


@register("square")
def square(x, attempt=1):
    return x * x


@register("flaky_until")
def flaky_until(x, succeed_at, attempt=1):
    if attempt < succeed_at:
        raise RuntimeError(f"transient failure on attempt {attempt}")
    return x * 10


@register("always_fails")
def always_fails(x, attempt=1):
    raise ValueError(f"permanent failure for {x} on attempt {attempt}")


def main():
    scheduler = Scheduler(num_workers=4)

    job_ids = []
    for i in range(5):
        job_ids.append(scheduler.submit("square", i))

    for i in range(3):
        job_ids.append(scheduler.submit("flaky_until", i, succeed_at=2 + (i % 2), max_retries=4))

    for i in range(2):
        job_ids.append(scheduler.submit("always_fails", i, max_retries=2))

    results = scheduler.run_to_completion(expected_jobs=len(job_ids))

    done = [r for r in results.values() if r.status == JobStatus.DONE]
    failed = [r for r in results.values() if r.status == JobStatus.FAILED]

    print(f"submitted {len(job_ids)} jobs across {scheduler.num_workers} workers\n")
    for job_id in sorted(results):
        r = results[job_id]
        print(f"job {job_id:>2}  {r.status.value:<6}  attempts={r.attempts}  "
              f"result={r.result!r}  error={r.error}")

    print(f"\nsummary: {len(done)} done, {len(failed)} failed, {len(job_ids)} total")

    retried = [jid for jid, hist in scheduler.history.items() if len(hist) > 2]
    print(f"jobs that needed a retry: {retried}")


if __name__ == "__main__":
    main()

SELECT name, salary FROM employees WHERE salary > 50000 ORDER BY salary DESC;

SELECT employees.name, departments.name FROM employees JOIN departments ON employees.dept_id = departments.dept_id WHERE salary > 40000 AND departments.dept_id != 30 ORDER BY employees.name;

SELECT * FROM departments;

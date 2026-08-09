extends RefCounted

# Minimal assertion helper for headless regression scripts.
# Usage: var t := preload("res://scripts/ecs_test_util.gd").new()

var total := 0
var failures := 0

func expect(cond: bool, name: String) -> void:
	total += 1
	if cond:
		print("  PASS: ", name)
	else:
		failures += 1
		printerr("  FAIL: ", name)

func expect_eq(a, b, name: String) -> void:
	expect(a == b, name + " (got " + str(a) + ", want " + str(b) + ")")

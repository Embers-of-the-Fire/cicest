#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"

readonly build_dir="${BUILD_DIR:-${repo_root}/build-prerelease}"
readonly dist_dir="${DIST_DIR:-${repo_root}/dist}"
readonly bundle_name="${BUNDLE_NAME:-cicest-x86_64-linux}"
readonly archive_path="${dist_dir}/${bundle_name}.tar.gz"

stage_dir="$(mktemp -d)"
smoke_dir="$(mktemp -d)"
trap 'rm -rf "${stage_dir}" "${smoke_dir}"' EXIT

prefix="${stage_dir}/${bundle_name}"
bundle_lib_dir="${prefix}/lib"
bundle_libexec_dir="${prefix}/libexec/cicest"
build_binaries=(
	"${build_dir}/compiler/cstc/cstc"
	"${build_dir}/compiler/cstc_inspect/cstc_inspect"
	"${build_dir}/compiler/cstc_repl/cstc_repl"
)
bundle_binaries=(
	"${prefix}/bin/cstc"
	"${prefix}/bin/cstc_inspect"
	"${prefix}/bin/cstc_repl"
)

mkdir -p "${dist_dir}"

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="${prefix}"

cmake --build "${build_dir}"
cmake --install "${build_dir}" --strip

mkdir -p "${bundle_lib_dir}"
mkdir -p "${bundle_libexec_dir}"

declare -A copied_shared_libs=()
for build_binary in "${build_binaries[@]}"; do
	while IFS= read -r shared_lib_path; do
		if [[ -z "${shared_lib_path}" || -n "${copied_shared_libs[${shared_lib_path}]:-}" ]]; then
			continue
		fi

		copied_shared_libs["${shared_lib_path}"]=1
		copied_shared_lib_path="${bundle_lib_dir}/$(basename "${shared_lib_path}")"
		cp -fL "${shared_lib_path}" "${copied_shared_lib_path}"
		chmod u+w "${copied_shared_lib_path}"
	done < <(ldd "${build_binary}" | awk '{for (i = 1; i <= NF; ++i) if ($i ~ /^\//) print $i}')
done

interpreter_path="$(patchelf --print-interpreter "${prefix}/bin/cstc")"
interpreter_name="$(basename "${interpreter_path}")"
cp -fL "${interpreter_path}" "${bundle_lib_dir}/${interpreter_name}"
chmod u+w "${bundle_lib_dir}/${interpreter_name}"

for bundle_binary in "${bundle_binaries[@]}"; do
	binary_name="$(basename "${bundle_binary}")"
	mv "${bundle_binary}" "${bundle_libexec_dir}/${binary_name}"
	cat >"${bundle_binary}" <<EOF
#!/bin/sh
set -eu

self_dir=\$(CDPATH= cd -- "\$(dirname -- "\$0")" && pwd)
bundle_root=\$(CDPATH= cd -- "\${self_dir}/.." && pwd)

exec "\${bundle_root}/lib/${interpreter_name}" \\
	--library-path "\${bundle_root}/lib" \\
	"\${bundle_root}/libexec/cicest/${binary_name}" "\$@"
EOF
	chmod 0755 "${bundle_binary}"
done

required_paths=(
	"${prefix}/bin/cstc"
	"${prefix}/bin/cstc_inspect"
	"${prefix}/bin/cstc_repl"
	"${prefix}/lib/${interpreter_name}"
	"${prefix}/lib/cicest/libcicest_rt.a"
	"${prefix}/libexec/cicest/cstc"
	"${prefix}/libexec/cicest/cstc_inspect"
	"${prefix}/libexec/cicest/cstc_repl"
	"${prefix}/share/cicest/std/prelude.cst"
)

for required_path in "${required_paths[@]}"; do
	if [[ ! -e "${required_path}" ]]; then
		echo "missing release bundle artifact: ${required_path}" >&2
		exit 1
	fi
done

tar -C "${stage_dir}" -czf "${archive_path}" "${bundle_name}"
tar -tzf "${archive_path}"

tar -C "${smoke_dir}" -xzf "${archive_path}"

smoke_output_binary="${smoke_dir}/hello_world"
smoke_expected_output="$(cat "${repo_root}/test/e2e/pass/basic/hello_world.stdout")"
smoke_actual_output="$(
	"${smoke_dir}/${bundle_name}/bin/cstc" \
		"${repo_root}/test/e2e/pass/basic/hello_world.cst" \
		-o "${smoke_output_binary}" >/dev/null
	"${smoke_output_binary}"
)"

if [[ "${smoke_actual_output}" != "${smoke_expected_output}" ]]; then
	echo "bundle smoke test output mismatch" >&2
	echo "expected: ${smoke_expected_output}" >&2
	echo "actual: ${smoke_actual_output}" >&2
	exit 1
fi

echo "created prerelease bundle: ${archive_path}"

# Program Execution

## Execution Flow

### Before Execution

The language has mainly three phases before any code goes to the VM or native compiler.
First, the frontend parses the source file and converts them to a stream of parsed tokens.
Second, the frontend constructs a HIR, which is de-sugared. The contract checker and generic checker works in this phase.
Finally, the frontend builds a LIR, which is relatively close to native LLVM IR with some more information.

### During Execution

If the program is running on the VM, the code will end up as a LIR.
The LIR is then interpreted by the VM directly.
If the program, instead, targeting the native platform, the LIR will then be converted to LLVM IR, and therefore compiled by LLVM.

## Generic Resolution and Compile Time Execution

The generic code and `const` code are executed based on a HIR.
The execution is lazy.
When a `const` code is going to be executed, it will be compiled to LIR and transferred to the VM.
If the compile time code depends on other compile time code, the caller of each will be tracked.
If a circular dependency is found, the compiler will throw an error.

## Contract Validation

The contract is validated during HIR lowering.

Given a keyword generic `keyword<K>`, generally there are the following rules:
1. A type is `keyword<K>` if one of the fields is `keyword` or `K` evaluates to `Contract::Success`.
2. A function is `keyword<K>` if one of the direct parameters or one of the direct statements is `keyword` or `K` evaluates to `Contract::Success`.
3. Branching expressions are always evaluated to `K: Contract::Success` if one of the branch has a contract of `keyword`.

Explicit contract narrowing is accepted.

## Asynchronous Execution

When a asynchronous operation is introduced in compile time context, an error will be reported.
The runtime asynchronous runtime is a polling runtime, based on a single-threaded model.
No multithread is supported in this language.

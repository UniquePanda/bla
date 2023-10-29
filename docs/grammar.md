$$

\begin{align}
    [\text{Prog}] &\to [\text{Stmt}]^* \\
    [\text{Stmt}] &\to
        \begin{cases}
            \text{let}\space\text{ident} \text{ '=' } \text{[Expr]}; \\
            \text{[BuiltInFunc]} \ \text{'('} \ \text{[Expr]} \ \text{')'}; \\
            \text{if} \ \text{'('} \ \text{[Expr]} \ \text{')'} \ [\text{Scope}] \\
            \quad (\text{elseif} \ \text{'('} \ \text{[Expr]} \ \text{')'} \ [\text{Scope}])^* \\
            \quad (\text{else} \ [\text{Scope}])? \\
            \text{[Scope]} \\
        \end{cases}
    \\
    [\text{Scope}] &\to \{[\text{Stmt}]^*\}\ \\
    [\text{Expr}] &\to
        \begin{cases}
            \text{UnExpr} \\
            \text{BinExpr} \\
            \text{Term} \\
        \end{cases}
    \\
     [\text{UnExpr}] &\to
        \begin{cases}
            [\text{PosUnExpr}] \\
            [\text{NegUnExpr}] \\
        \end{cases}
    \\
    [\text{PosUnExpr}] &\to \text{ '+' } [\text{Expr}] \\
    [\text{NegUnExpr}] &\to \text{ '-' } [\text{Expr}] \\
    [\text{BinExpr}] &\to
        \begin{cases}
            [\text{SumBinExpr}] \\
            [\text{SubBinExpr}] \\
            [\text{MulBinExpr}] \\
            [\text{DivBinExpr}] \\
        \end{cases}
    \\
    [\text{SumBinExpr}] &\to
        \begin{cases}
            [\text{Expr}] \text{ '+' } [\text{Expr}] \\
            [\text{Expr}] \text{ '+' } [\text{UnExpr}] \\
            [\text{Expr}] \text{ '+' } [\text{MulBinExpr}] \\
            [\text{Expr}] \text{ '+' } [\text{DivBinExpr}] \\
        \end{cases}
    \\
    [\text{SubBinExpr}] &\to
        \begin{cases}
            [\text{Expr}] \text{ '-' } [\text{Expr}] \\
            [\text{Expr}] \text{ '-' } [\text{UnExpr}] \\
            [\text{Expr}] \text{ '-' } [\text{MulBinExpr}] \\
            [\text{Expr}] \text{ '-' } [\text{DivBinExpr}] \\
        \end{cases}
    \\
    [\text{MulBinExpr}] &\to
        \begin{cases}
            [\text{Expr}] \text{ '*' } [\text{Expr}] \\
            [\text{Expr}] \text{ '*' } [\text{UnExpr}] \\
        \end{cases}
    \\
    [\text{DivBinExpr}] &\to
        \begin{cases}
            [\text{Expr}] \text{ '/' } [\text{Expr}] \\
            [\text{Expr}] \text{ '/' } [\text{UnExpr}] \\
        \end{cases}
    \\
    [\text{Term}] &\to
        \begin{cases}
            \text{ident} \\
            \text{int\_lit} \\
            \text{dbl\_lit} \\
            \text{str\_lit} \\
            \text{bool\_lit} \\
            \text{'('} \ [\text{Expr}] \ \text{')'} \\
        \end{cases}
    \\
    [\text{BuiltInFunc}] &\to
        \begin{cases}
            \text{exit'('int\_lit')'} & \text{Exit program with given exit code} \\
            \text{print'('[Expr]')'} & \text{Print given input to stdout} \\
            \text{println'('[Expr]')'} & \text{Print given input with added line break to stdout} \\
        \end{cases}
    \\
\end{align}

$$
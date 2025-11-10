import js from '@eslint/js'
import react from 'eslint-plugin-react'

export default [
  {
    files: ['**/*.{js,jsx}'],
    ignores: ['coverage/**', 'dist/**'],
    languageOptions: {
      ecmaVersion: 2020,
      sourceType: 'module',
      globals: {
        window: 'readonly',
        document: 'readonly',
        navigator: 'readonly',
        console: 'readonly',
        setTimeout: 'readonly',
        setInterval: 'readonly',
        clearInterval: 'readonly',
        crypto: 'readonly',
        URL: 'readonly',
        Blob: 'readonly',
        MediaRecorder: 'readonly',
        alert: 'readonly',
        confirm: 'readonly',
        expect: 'readonly',
        afterEach: 'readonly',
        Date: 'readonly'
      },
      parserOptions: {
        ecmaFeatures: {
          jsx: true,
        },
      },
    },
    plugins: {
      react,
    },
    rules: {
      ...js.configs.recommended.rules,
      'react/react-in-jsx-scope': 'off',
      'react/prop-types': 'off',
      'no-unused-vars': 'warn',
    },
    settings: {
      react: {
        version: 'detect',
      },
    },
  }
]
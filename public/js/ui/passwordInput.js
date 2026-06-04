// SPDX-License-Identifier: MIT

// Copyright 2026 chargebyte GmbH

export function renderPasswordInput(options = {}) {
  const rootElement = document.createElement('div');
  rootElement.className = options.className || 'password-input-pair';

  const firstField = createPasswordField({
    id: options.id ? `${options.id}-primary` : '',
    placeholder: options.primaryPlaceholder || 'Password'
  });
  const secondField = createPasswordField({
    id: options.id ? `${options.id}-confirm` : '',
    placeholder: options.confirmPlaceholder || 'Repeat password'
  });

  rootElement.appendChild(firstField.element);
  rootElement.appendChild(secondField.element);

  const updateValidationState = () => {
    const firstValue = firstField.input.value;
    const secondValue = secondField.input.value;

    if (firstValue === '' && secondValue === '') {
      applyValidationState(firstField.input, '');
      applyValidationState(secondField.input, '');
      return;
    }

    if (firstValue === secondValue) {
      applyValidationState(firstField.input, 'match');
      applyValidationState(secondField.input, 'match');
      return;
    }

    applyValidationState(firstField.input, 'mismatch');
    applyValidationState(secondField.input, 'mismatch');
  };

  firstField.input.addEventListener('input', updateValidationState);
  secondField.input.addEventListener('input', updateValidationState);

  Object.defineProperty(rootElement, 'value', {
    configurable: true,
    enumerable: true,
    get() {
      return firstField.input.value;
    },
    set(nextValue) {
      const normalizedValue = nextValue ?? '';
      firstField.input.value = normalizedValue;
      secondField.input.value = normalizedValue;
      updateValidationState();
    }
  });

  if (typeof options.id === 'string' && options.id.trim() !== '') {
    rootElement.id = options.id;
  }

  if (typeof options.value === 'string') {
    rootElement.value = options.value;
  } else {
    updateValidationState();
  }

  return rootElement;
}

function createPasswordField(options = {}) {
  const element = document.createElement('div');
  element.className = 'password-input-field';

  const inputElement = document.createElement('input');
  inputElement.type = 'password';
  inputElement.className = 'input password-input-control';
  inputElement.placeholder = options.placeholder || '';

  if (typeof options.id === 'string' && options.id.trim() !== '') {
    inputElement.id = options.id;
  }

  const revealButton = document.createElement('button');
  revealButton.type = 'button';
  revealButton.className = 'btn btn-peek password-reveal-button';
  revealButton.setAttribute('aria-label', 'Show password');
  revealButton.title = 'Show password';

  const iconElement = document.createElement('span');
  iconElement.className = 'password-reveal-icon';
  iconElement.textContent = '◉';
  revealButton.appendChild(iconElement);

  const showPassword = () => {
    inputElement.type = 'text';
  };
  const hidePassword = () => {
    inputElement.type = 'password';
  };

  revealButton.addEventListener('mousedown', showPassword);
  revealButton.addEventListener('mouseup', hidePassword);
  revealButton.addEventListener('mouseleave', hidePassword);
  revealButton.addEventListener('touchstart', showPassword, { passive: true });
  revealButton.addEventListener('touchend', hidePassword);
  revealButton.addEventListener('touchcancel', hidePassword);

  element.appendChild(inputElement);
  element.appendChild(revealButton);

  return {
    element,
    input: inputElement
  };
}

function applyValidationState(inputElement, state) {
  inputElement.classList.remove('password-input-match', 'password-input-mismatch');

  if (state === 'match') {
    inputElement.classList.add('password-input-match');
    return;
  }

  if (state === 'mismatch') {
    inputElement.classList.add('password-input-mismatch');
  }
}

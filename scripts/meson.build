
extension_marshal_cmd = find_program('generate-ext-marshal')

extension_marshal_generator = generator(extension_marshal_cmd,
                                        output  : '@BASENAME@-marshal.h',
                                        arguments : ['@INPUT@', '@OUTPUT@'])
